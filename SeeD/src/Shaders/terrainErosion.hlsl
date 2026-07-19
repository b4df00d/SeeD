#include "structs.hlsl"

cbuffer CustomErosion : register(b3)
{
    HLSL::TerrainErosionParameters erosion;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

/*
=====================================================================================

Runevision "Advanced Terrain Erosion Filter", ported 1:1 (GLSL -> HLSL) from the
shadertoy reference. Function names, argument
order, comments and line structure are kept verbatim so the two can be diffed;
only the syntax differs (vec->float, fract->frac, mix->lerp) plus:
 - hash() is a local stand-in for the shadertoy Common-tab hash (different random
   pattern than the demo, same statistics),
 - the Heightmap() demo scaffolding is replaced by TerrainErosionMain, which reads
   the terrain's input heightmap and writes the eroded + difference maps.

For more on the technique, see:
https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html

Dispatched by TerrainErosion::Render (Renderer.h), one dispatch per terrain, in
normalized [0,1] terrain UV space (p = uv; world position = (p - 0.5) * worldExtent,
matching terrainmesh.hlsl). Stateless: every texel is independent, no ping-pong.

=====================================================================================
*/


// -----------------------------------------------------------------------------
// PHACELLE NOISE FUNCTION
// -----------------------------------------------------------------------------

#define TAU 6.28318530717959

float clamp01(float t) { return clamp(t, 0.0, 1.0); }

// Stand-in for the shadertoy Common-tab 'hash': integer-lattice hash (pcg2d-flavored)
// -> [-1, 1]^2, stable across the whole map. Inputs are integer-valued grid points.
float2 hash(float2 p)
{
    uint2 v = uint2(int2(p)) * uint2(1664525u, 1013904223u);
    v.x += v.y * 1664525u;
    v.y += v.x * 1013904223u;
    v ^= v >> 16;
    v.x += v.y * 1664525u;
    v.y += v.x * 1013904223u;
    v ^= v >> 16;
    return float2(v & 0xFFFFFFu) / 16777216.0 * 2.0 - 1.0;
}

// The Simple Phacelle Noise function produces a stripe pattern aligned with the input vector.
// The name Phacelle is a portmanteau of phase and cell, since the function produces a phase by
// interpolating cosine and sine waves from multiple cells.
//  - p is the input point being evaluated.
//  - normDir is the direction of the stripes at this point. It must be a normalized vector.
//  - freq is the freqency of the stripes within each cell. It's best to keep it close to 1.0, as
//    high values will produce distortions and other artifacts.
//  - offset is the phase offset of the stripes, where 1.0 is a full cycle.
//  - normalization is the degree of normalization applied, between 0 and 1. With e.g. a value of
//    0.4, raw output with a magnitude below 0.6 won't get fully normalized to a magnitude of 1.0.
// Phacelle Noise function copyright (c) 2025 Rune Skovbo Johansen
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
float4 PhacelleNoise(in float2 p, float2 normDir, float freq, float offset, float normalization) {
    // Get a vector orthogonal to the input direction, with a
    // magnitude proportional to the frequency of the stripes.
    float2 sideDir = normDir.yx * float2(-1.0, 1.0) * freq * TAU;
    offset *= TAU;

    // Iterate over 4x4 cells, calculating a stripe pattern for each and blending between them.
    // pInt is the integer part of the current coordinate p, pFrac is the remainder.
    //
    // o   o   o   o
    //
    // o   o   o   o
    //       p
    // o   i   o   o
    //
    // o   o   o   o
    //
    // p: current coordinate    i: integer part of p    o: grid points for 4x4 cells
    //
    float2 pInt = floor(p);
    float2 pFrac = frac(p);
    float2 phaseDir = float2(0.0, 0.0);
    float weightSum = 0.0;
    for (int i = -1; i <= 2; i++) {
        for (int j = -1; j <= 2; j++) {
            float2 gridOffset = float2(i, j);

            // Calculate a cell point by starting off with a point in the integer grid.
            float2 gridPoint = pInt + gridOffset;

            // Calculate a random offset for the cell point between -0.5 and 0.5 on each axis.
            float2 randomOffset = hash(gridPoint) * 0.5;

            // The final cell point (we don't store it) is the gridPoint plus the randomOffset.
            // Calculate a vector representing the input point relative to this cell point:
            // p - (gridPoint + randomOffset)
            // = (pFrac + pInt) - ((pInt + gridOffset) + randomOffset)
            // = pFrac + pInt - pInt - gridOffset - randomOffset
            // = pFrac - gridOffset - randomOffset
            float2 vectorFromCellPoint = pFrac - gridOffset - randomOffset;

            // Bell-shaped weight function which is 1 at dist 0 and nearly 0 at dist 1.5.
            // Due to the random offsets of up to 0.5, the closest a cell point not in the 4x4
            // grid can be to the current point p is 1.5 units away.
            float sqrDist = dot(vectorFromCellPoint, vectorFromCellPoint);
            float weight = exp(-sqrDist * 2.0);
            // Subtract 0.01111 to make the function actually 0 at distance 1.5, which avoids
            // some (very subtle) grid line artefacts.
            weight = max(0.0, weight - 0.01111);

            // Keep track of the total sum of weights.
            weightSum += weight;

            // The waveInput is a gradient which increases in value along sideDir. Its rate of
            // change is the freq times tau, due to the multiplier pre-applied to sideDir.
            float waveInput = dot(vectorFromCellPoint, sideDir) + offset;

            // Add this cell's cosine and sine wave contributions to the interpolated value.
            phaseDir += float2(cos(waveInput), sin(waveInput)) * weight;
        }
    }

    // Get the raw interpolated value.
    float2 interpolated = phaseDir / weightSum;
    // Interpret the value as a vector whose length represents the magnitude of both waves.
    float magnitude = sqrt(dot(interpolated, interpolated));
    // Apply a lower threshold to show small magnitudes we're going to fully normalize.
    magnitude = max(1.0 - normalization, magnitude);
    // Return a vector containing the normalized cosine and sine waves, as well as the direction
    // vector, which can be multiplied onto the sine to get the derivatives of the cosine.
    return float4(interpolated / magnitude, sideDir);
}


// -----------------------------------------------------------------------------
// EROSION FUNCTION
// -----------------------------------------------------------------------------

// First a few utility functions.

float pow_inv(float t, float power) {
    // Flip, raise to the specified power, and flip back.
    return 1.0 - pow(1.0 - clamp01(t), power);
}

float ease_out(float t) {
    // Flip by subtracting from one.
    float v = 1.0 - clamp01(t);
    // Raise to a power of two and flip back.
    return 1.0 - v * v;
}

float smooth_start(float t, float smoothing) {
    if (t >= smoothing)
        return t - 0.5 * smoothing;
    return 0.5 * t * t / smoothing;
}

float2 safe_normalize(float2 n) {
    // A div-by-zero-safe replacement for normalize.
    float l = length(n);
    return (abs(l) > 1e-10) ? (n / l) : n;
}

// Advanced Terrain Erosion Filter copyright (c) 2025 Rune Skovbo Johansen
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
float4 ErosionFilter(
    // Input parameters that vary per pixel.
    in float2 p, float3 heightAndSlope, float fadeTarget,
    // Stylistic parameters that may vary per pixel.
    float strength, float gullyWeight, float detail, float4 rounding, float4 onset, float2 assumedSlope,
    // Scale related parameters that do not support variation per pixel.
    float scale, int octaves, float lacunarity,
    // Other parameters.
    float gain, float cellScale, float normalization,
    // Output parameters.
    out float ridgeMap, out float debug
) {
    strength *= scale;
    fadeTarget = clamp(fadeTarget, -1.0, 1.0);

    float3 inputHeightAndSlope = heightAndSlope;
    float freq = 1.0 / (scale * cellScale);
    float slopeLength = max(length(heightAndSlope.yz), 1e-10);
    float magnitude = 0.0;
    float roundingMult = 1.0;

    float roundingForInput = lerp(rounding.y, rounding.x, clamp01(fadeTarget + 0.5)) * rounding.z;
    // The combined accumulating mask, based first on initial slope, and later on slope of each octave too.
    float combiMask = ease_out(smooth_start(slopeLength * onset.x, roundingForInput * onset.x));

    // Initialize the ridgeMap fadeTarget and mask.
    float ridgeMapCombiMask = ease_out(slopeLength * onset.z);
    float ridgeMapFadeTarget = fadeTarget;

    // Deteriming the strength of the initial slope used for gully directions
    // based on the specified mix of the actual slope and an assumed slope.
    float2 gullySlope = lerp(heightAndSlope.yz, heightAndSlope.yz / slopeLength * assumedSlope.x, assumedSlope.y);

    for (int i = 0; i < octaves; i++) {
        // Calculate and add gullies to the height and slope.
        float4 phacelle = PhacelleNoise(p * freq, safe_normalize(gullySlope), cellScale, 0.25, normalization);
        // Multiply with freq since p was multiplied with freq.
        // Negate since we use slope directions that point down.
        phacelle.zw *= -freq;
        // Amount of slope as value from 0 to 1.
        float sloping = abs(phacelle.y);

        // Add non-masked, normalized slope to gullySlope, for use by subsequent octaves.
        // It's normalized to use the steepest part of the sine wave everywhere.
        gullySlope += sign(phacelle.y) * phacelle.zw * strength * gullyWeight;

        // Handle height offset and approximate output slope.

        // Gullies has height offset (from -1 to 1) in x and derivative in yz.
        float3 gullies = float3(phacelle.x, phacelle.y * phacelle.zw);
        // Fade gullies towards fadeTarget based on combiMask.
        float3 fadedGullies = lerp(float3(fadeTarget, 0.0, 0.0), gullies * gullyWeight, combiMask);
        // Apply height offset and derivative (slope) according to strength of current octave.
        heightAndSlope += fadedGullies * strength;
        magnitude += strength;

        // Update fadeTarget to include the new octave.
        fadeTarget = fadedGullies.x;

        // Update the mask to include the new octave.
        float roundingForOctave = lerp(rounding.y, rounding.x, clamp01(phacelle.x + 0.5)) * roundingMult;
        float newMask = ease_out(smooth_start(sloping * onset.y, roundingForOctave * onset.y));
        combiMask = pow_inv(combiMask, detail) * newMask;

        // Update the ridgeMap fadeTarget and mask.
        ridgeMapFadeTarget = lerp(ridgeMapFadeTarget, gullies.x, ridgeMapCombiMask);
        float newRidgeMapMask = ease_out(sloping * onset.w);
        ridgeMapCombiMask = ridgeMapCombiMask * newRidgeMapMask;

        // Prepare the next octave.
        strength *= gain;
        freq *= lacunarity;
        roundingMult *= rounding.w;
    }

    ridgeMap = ridgeMapFadeTarget * (1.0 - ridgeMapCombiMask);
    debug = fadeTarget;

    float3 heightAndSlopeDelta = heightAndSlope - inputHeightAndSlope;
    return float4(heightAndSlopeDelta, magnitude);
}


// -----------------------------------------------------------------------------
// COMPUTE ENTRY (replaces the shadertoy's Heightmap() demonstration section)
// -----------------------------------------------------------------------------

#pragma compute Erosion TerrainErosionMain

[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 1)]
void TerrainErosionMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= erosion.outputResolution || dtid.y >= erosion.outputResolution)
        return;

    // p is the texel-center UV: the filter runs in normalized [0,1] terrain space like the
    // shadertoy (terrainmesh.hlsl samples with uv = worldPos.xz / worldExtent + 0.5).
    float2 p = (float2(dtid.xy) + 0.5) / erosion.outputResolution;

    // Demo constants kept verbatim from the shadertoy Heightmap() (not exposed on the component):
    //  ROUNDING zw: multipliers applied to the input mask / to each subsequent octave.
    //  ONSET: how far away from ridges/creases the erosion takes effect (input, octave, and the
    //  two ridgeMap-specific variants).
    //  ASSUMED_SLOPE: override of the input slope magnitude (x) and its blend amount (y).
    const float4 EROSION_ROUNDING = float4(erosion.ridgeRounding, erosion.creaseRounding, 0.1, 2.0);
    const float4 EROSION_ONSET = float4(0.7, 1.25, 2.8, 1.5);
    const float2 EROSION_ASSUMED_SLOPE = float2(0.7, 1.0);
    const float2 TERRAIN_HEIGHT_OFFSET = float2(0.0, 0.0);
    const float DEFAULT_HEIGHT = 0.5;

    // Get height and slope from the input heightmap (the demo reads painted values with analytic
    // slopes; here: red channel + central differences in p units, height low-passed over the 4
    // gradient taps).
    Texture2D<float4> src = ResourceDescriptorHeap[erosion.inputHeightmapIndex];
    float srcW, srcH;
    src.GetDimensions(srcW, srcH);
    float2 texel = float2(1.0 / max(srcW, 1.0), 1.0 / max(srcH, 1.0));
    float hX1 = src.SampleLevel(samplerLinearClamp, p + float2(texel.x, 0), 0).x;
    float hX0 = src.SampleLevel(samplerLinearClamp, p - float2(texel.x, 0), 0).x;
    float hY1 = src.SampleLevel(samplerLinearClamp, p + float2(0, texel.y), 0).x;
    float hY0 = src.SampleLevel(samplerLinearClamp, p - float2(0, texel.y), 0).x;
    float3 n = float3(
        (hX1 + hX0 + hY1 + hY0) / 4.0,
        (hX1 - hX0) / (2.0 * texel.x),
        (hY1 - hY0) / (2.0 * texel.y));

    // Define the erosion fade target based on the altitude of the pre-eroded terrain.
    // The fade target should strive to be -1 at valleys and 1 at peaks, but overshooting is ok.
    float fadeTarget = clamp((n.x - DEFAULT_HEIGHT) / 0.15, -1.0, 1.0);

    // Store erosion in h (x : height delta, yz : slope delta, w : magnitude).
    // The output ridge map is -1 on creases and 1 on ridges.
    float ridgeMap, debug;
    float4 h = ErosionFilter(
        p, n, fadeTarget,
        erosion.strength, erosion.gullyWeight, erosion.detail,
        EROSION_ROUNDING, EROSION_ONSET, EROSION_ASSUMED_SLOPE,
        erosion.scale, int(erosion.octaves), erosion.lacunarity,
        erosion.gain, erosion.cellScale, erosion.normalization,
        ridgeMap, debug);

    // Offset according to the height offset parameter by multiplying it with the magnitude.
    float offset = lerp(TERRAIN_HEIGHT_OFFSET.x, -fadeTarget, TERRAIN_HEIGHT_OFFSET.y) * h.w;
    float eroded = n.x + h.x + offset;

    // [0,1] raw-height pipeline contract; CPU culling pads bounds by
    // strength * scale * sum(gain^o) * max(1, gullyWeight) (Terrain.h ComputeNodeBoundingSphere).
    RWTexture2D<float> dst = ResourceDescriptorHeap[erosion.outputHeightmapIndex];
    dst[dtid.xy] = saturate(eroded);

    // Difference map (R8): erosion delta normalized by the summed octave magnitude, centered at
    // 0.5 (same encoding as the demo's packed h.x / h.w channel): 0.5 = untouched, < 0.5 carved,
    // > 0.5 deposited. Displayed on the terrain albedo (terrainmesh.hlsl).
    RWTexture2D<float> dstDiff = ResourceDescriptorHeap[erosion.outputDiffIndex];
    dstDiff[dtid.xy] = clamp01(h.x / max(h.w, 1e-5) * 2 * 0.5 + 0.5);
    dstDiff[dtid.xy] = (abs(h.x) + abs(h.y)) * 0.08 + (ridgeMap + 0.02);

    // ridgeMap (-1 creases .. 1 ridges, useful for drainage/tree placement) is computed but not
    // stored yet -- future third output map.
}
