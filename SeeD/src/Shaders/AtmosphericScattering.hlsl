#include "structs.hlsl"

#define SHARC_ENABLE_64_BIT_ATOMICS     1
#define SHARC_SEPARATE_EMISSIVE         1
#include "SharcCommon.h"

cbuffer CustomRT : register(b3)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

cbuffer CustomAS : register(b4)
{
    HLSL::AtmosphericScatteringParameters asParameters;
};
#define CUSTOM_ROOT_BUFFER_2

#include "binding.hlsl"
#include "common.hlsl"

#define TRACING_DISTANCE                1000.0f
#define FP16_MAX                        65000.0f // slightly under R16F's 65504
#define MAX_OPTICAL_DEPTH               50.0f
#define FOG_NOISE                       1 // 0: uniform fog (height falloff only), the noise parameters are ignored
#define TEMPORAL_BLEND                  0.05f // weight of the new frame in the froxel history (~20 frames)
#define SPATIAL_FILTER                  1 // 3x3 tent on the history when integrating it (not written back)
#define DEPTH_AWARE_SLICES              1 // a slice holding the visible surface is sampled only in front of it (see RayGen)
#define DEPTH_CLAMP_BEHIND              1 // with DEPTH_AWARE_SLICES: this many slices right behind the one holding the visible surface are sampled just in front of it (no light from beyond walls), 0: none
#define FOG_INDIRECT_SHARC              0 // 1: an indirect ray hitting a surface takes that surface's SHARC radiance, 0: it counts as black (sky only)
#define FOG_DEBUG_LIGHTS                0 // 0: analytic lights + indirect, 1: analytic lights only (RIS + shadow ray), 2: indirect only (sky + SHARC)
#define BOUNCES_MIN                     1
#define RIS_CANDIDATES_LIGHTS           8 // Number of candidates used for resampling of analytical lights
#define SHADOW_RAY_IN_RIS               0 // Enable this to cast shadow rays for each candidate during resampling. This is expensive but increases quality
#define ENABLE_SPECULAR_LOBE            0 // Enable to use the specular lobe for splitting between diffuse and specular BRDFs

#include "raytracingCommon.hlsl"

#pragma raytracing Update

GlobalRootSignature SeeDRootSignatureRT =
{
    SeeDRootSignature
};

// Direction distributed as HenyeyGreensteinPhase(g, dot(dir, axis)): forward (g > 0) around axis
float3 SampleHenyeyGreenstein(float g, float3 axis, float2 u)
{
    float cosTheta;
    if (abs(g) < 1e-3f)
        cosTheta = 1.0f - 2.0f * u.x;
    else
    {
        float s = (1.0f - g * g) / (1.0f - g + 2.0f * g * u.x);
        cosTheta = (1.0f + g * g - s * s) / (2.0f * g);
    }
    cosTheta = clamp(cosTheta, -1.0f, 1.0f);
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    float phi = 2.0f * PI * u.y;
    float3 bitangent = normalize(GetPerpendicularVector(axis));
    float3 tangent = cross(bitangent, axis);
    return normalize(axis * cosTheta + (tangent * cos(phi) + bitangent * sin(phi)) * sinTheta);
}

// Read-only view of the SHARC radiance cache, built exactly as raytracing2.hlsl does (same camera
// for the grid levels, or the lookups miss its entries)
SharcParameters GetSharcParameters()
{
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];

    SharcParameters sharcParameters;
    sharcParameters.gridParameters.cameraPosition = cameras[0].worldPos.xyz;
    sharcParameters.gridParameters.sceneScale = rtParameters.SHARCSceneScale;
    sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
    sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;
    sharcParameters.hashMapData.capacity = rtParameters.SHARCEntriesNum;
    sharcParameters.hashMapData.hashEntriesBuffer = ResourceDescriptorHeap[rtParameters.SHARCHashEntriesBufferIndex];
    sharcParameters.accumulationBuffer = ResourceDescriptorHeap[rtParameters.SHARCAccumulationBufferIndex];
    sharcParameters.resolvedBuffer = ResourceDescriptorHeap[rtParameters.SHARCResolvedBufferIndex];
    sharcParameters.radianceScale = rtParameters.SHARCRadianceScale;
    sharcParameters.enableAntiFireflyFilter = rtParameters.SHARCEnableAntifirefly;
    return sharcParameters;
}

// Density of the global fog (RenderSettingsVolume) at a world position, multiplies fogScattering /
// fogAbsorption
float FogDensity(float3 worldPos)
{
    float heightDensity = min(exp(-(worldPos.y - asParameters.fogAbsorption.w) * asParameters.heightFalloff), 1.0f);

    float noiseDensity = 1.0f;
#if FOG_NOISE
    float noise = PerlinNoise3_Normalized(worldPos * asParameters.noiseFrequency + viewContext.frameTime * asParameters.animationSpeed * asParameters.noiseFrequency);
    noiseDensity = saturate((noise - asParameters.noiseThresholdLow) / max(asParameters.noiseThresholdHigh - asParameters.noiseThresholdLow, 1e-4f));
    noiseDensity = noiseDensity * noiseDensity * (3.0f - 2.0f * noiseDensity); // smoothstep, without its undefined low == high case
#endif

    return asParameters.fogDensity * heightDensity * noiseDensity;
}

#define HAZE_FADE_START         600     // frames without a splat before a haze map texel starts fading out
#define HAZE_FADE_FRAMES        300
#define HAZE_EDGE_FADE          16.0f   // texels: the coarsest level fades out toward its window border
#define HAZE_LEVEL_BLEND        16.0f   // texels: a level blends into the next coarser one over its window border

// One level of a haze layer's map at a world position: the density profile (0..1, before noise) above
// the highest surface splatted in this texel; inWindow: the position is inside this level's window,
// edge: its distance in texels to the window border
float SurfaceHazeLevel(HLSL::HazeLayer layer, Texture2D<uint2> map, uint level, float3 worldPos, out bool inWindow, out float edge)
{
    // the read map was resolved last frame, with last frame's windows
    float texelSize = HazeTexelSize(layer, level);
    int2 origin = HazeOrigin(layer.previousOrigin, level);
    int2 cell = HazeCell(worldPos.xz, texelSize);
    int2 local = cell - origin;
    int2 edges = min(local, int(layer.resolution) - 1 - local);
    edge = float(min(edges.x, edges.y));
    uint2 texel;
    inWindow = HazeTexel(cell, origin, level, layer.resolution, texel);
    if (!inWindow)
        return 0.0f;
    uint2 value = map[texel];
    if (value.x == 0)
        return 0.0f;

    float coverage = saturate(1.0f - (float(value.y) - HAZE_FADE_START) / HAZE_FADE_FRAMES) * layer.coverage;

    // exponential above the surface; full inside the slab, plus one texel of margin since the
    // texel stores the highest point of its footprint (slopes); nothing further below (under water)
    float h = worldPos.y - HazeHeight(value.x);
    float profile = h >= 0.0f ? exp(-h / max(layer.height, 1e-3f)) : (h > -(layer.slab + texelSize) ? 1.0f : 0.0f);
    return profile * coverage;
}

// One haze layer at a world position: its medium above the highest surface splatted in its map at
// this xz. The map's levels are the same surfaces at two resolutions (a fine 128 m window inside a
// coarse 1 km one), not separate media: inside the fine window only the fine level counts, an empty
// fine texel means this layer has no surface there (falling back to the coarse level would smear
// haze from the coarse texel's highest neighbour around it). The coarse level takes over outside,
// blended over the fine window's border. The layers themselves add up in SurfaceHaze
void SurfaceHazeLayer(HLSL::HazeLayer layer, float3 worldPos, float time, inout float3 sigmaS, inout float3 sigmaT, inout float gWeighted)
{
    Texture2D<uint2> map = ResourceDescriptorHeap[layer.readIndex];
    bool fineInWindow, coarseInWindow;
    float fineEdge, coarseEdge;
    float fine = SurfaceHazeLevel(layer, map, 0, worldPos, fineInWindow, fineEdge);
    float fineWeight = fineInWindow ? saturate(fineEdge / HAZE_LEVEL_BLEND) : 0.0f;
    float density = fine;
    if (fineWeight < 1.0f)
    {
        float coarse = SurfaceHazeLevel(layer, map, 1, worldPos, coarseInWindow, coarseEdge);
        coarse *= saturate(coarseEdge / HAZE_EDGE_FADE);
        density = lerp(coarse, fine, fineWeight);
    }
    if (density <= 0.0f)
        return;

    float3 p = (worldPos - layer.windG.xyz * time) * layer.noiseFrequency;
    float noise = saturate((PerlinNoise3_Normalized(p) - layer.noiseThresholdLow) / max(layer.noiseThresholdHigh - layer.noiseThresholdLow, 1e-4f));
    noise = noise * noise * (3.0f - 2.0f * noise);

    float extinction = layer.albedoDensity.w * density * noise;
    float3 scattering = layer.albedoDensity.xyz * extinction;
    sigmaS += scattering;
    sigmaT += extinction;
    gWeighted += layer.windG.w * dot(scattering, 1.0f / 3.0f);
}

// Surface haze (structs.hlsl HazeMap / HazeLayer): every active layer, they stack (dust on the
// ground under pollen in the canopy). gWeighted accumulates g * mean scattering, FogMedium divides it back
void SurfaceHaze(float3 worldPos, inout float3 sigmaS, inout float3 sigmaT, inout float gWeighted)
{
    HLSL::HazeMap hazeMap = viewContext.hazeMap;
    if (hazeMap.layersIndex == HLSL::invalidUINT)
        return;
    StructuredBuffer<HLSL::HazeLayer> layers = ResourceDescriptorHeap[hazeMap.layersIndex];
    uint mask = hazeMap.activeMask;
    while (mask != 0)
    {
        uint layerIndex = firstbitlow(mask);
        mask &= mask - 1;
        HLSL::HazeLayer layer = layers[layerIndex];
        if (layer.readable != 0)
            SurfaceHazeLayer(layer, worldPos, hazeMap.time, sigmaS, sigmaT, gWeighted);
    }
}

// Fog medium at a world position: the global fog plus every local source. The single place to add
// density sources (particles, a painted fog map, ...). g: the Henyey-Greenstein anisotropy of the
// mix, each source's g weighted by its scattering
void FogMedium(float3 worldPos, out float3 sigmaS, out float3 sigmaT, out float g)
{
    // Global fog. Its height falloff never reaches 0: below 1/1000 of the global density counts as
    // none (~350 m above fogHeight at the default falloff), so empty froxels skip their rays
    float density = FogDensity(worldPos);
    if (density < 1e-3f * asParameters.fogDensity)
        density = 0.0f;
    sigmaS = asParameters.fogScattering.xyz * density;
    sigmaT = sigmaS + asParameters.fogAbsorption.xyz * density;

    // FogVolume spheres: extinction w * (1 - d^2/r^2)^2, scattering albedo color (absorbs 1 - color)
    if (commonResourcesIndices.fogVolumeCount > 0)
    {
        StructuredBuffer<HLSL::FogVolume> volumes = ResourceDescriptorHeap[commonResourcesIndices.fogVolumesHeapIndex];
        for (uint i = 0; i < commonResourcesIndices.fogVolumeCount; i++)
        {
            HLSL::FogVolume volume = volumes[i];
            float3 offset = worldPos - volume.sphere.xyz;
            float f = 1.0f - dot(offset, offset) / (volume.sphere.w * volume.sphere.w);
            if (f <= 0.0f)
                continue;
            float extinction = volume.colorDensity.w * f * f;
            sigmaS += volume.colorDensity.xyz * extinction;
            sigmaT += extinction;
        }
    }

    // the global fog and the fog volumes share the global g
    float globalG = asParameters.fogScattering.w;
    float gWeighted = globalG * dot(sigmaS, 1.0f / 3.0f);
    SurfaceHaze(worldPos, sigmaS, sigmaT, gWeighted);
    float scatteringMean = dot(sigmaS, 1.0f / 3.0f);
    g = scatteringMean > 0.0f ? clamp(gWeighted / scatteringMean, -0.999f, 0.999f) : globalG;
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 launchIndex = DispatchRaysIndex().xy;
    const uint2 launchDimensions = DispatchRaysDimensions().xy;
    uint rngState = InitRNG(launchIndex, launchDimensions, viewContext.frameTime ^ JenkinsHash(DispatchRaysIndex().z));

    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[asParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[asParameters.currentFroxelIndex];
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];

    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];

    RaytracingAccelerationStructure AccelerationStructure = ResourceDescriptorHeap[rtParameters.BVH];

    // Slice z holds the medium of the segment (z - 1, z] that AtmosphericScatteringAccumulation
    // integrates: jittered inside it, the temporal reprojection averages it out. Slice 0 is the
    // camera itself, an empty segment
    if (DispatchRaysIndex().z == 0)
    {
        froxelData[DispatchRaysIndex().xyz] = 0.0f;
        return;
    }
    // Low discrepancy over frames (R1 in z, R2 in xy, each offset per froxel by a hash) so ~20 frames
    // of history cover the cell evenly instead of clumping like white noise
    uint froxelHash = JenkinsHash(DispatchRaysIndex().x + (DispatchRaysIndex().y + DispatchRaysIndex().z * launchDimensions.y) * launchDimensions.x);
    uint frame = viewContext.frameNumber;
    float jitterZ = ((froxelHash + frame * 2654435769u) >> 8) / 16777216.0f;                                  // golden ratio
    float2 jitterXY = float2(((JenkinsHash(froxelHash) + frame * 3242174889u) >> 8) / 16777216.0f,         // plastic number
                             ((JenkinsHash(froxelHash + 1u) + frame * 2447445413u) >> 8) / 16777216.0f) - 0.5f;
    float3 froxelPos = DispatchRaysIndex().xyz;
    froxelPos.xy += jitterXY;
    froxelPos.z -= jitterZ;
    float3 worldPos = FroxelToWorld(froxelPos, currentFroxel.resolution.xyz, asParameters.specialNear, camera);
    float3 viewDir = FroxelViewDir(froxelPos.xy, currentFroxel.resolution.xy, camera);

#if DEPTH_AWARE_SLICES
    // The composite reads each pixel at its own depth, interpolating inside the slice that holds its
    // surface. If the slice is sampled over its whole depth, a medium hugging the surface (surface
    // haze) is averaged with the empty or occluded part behind it, and how much of it a pixel gets
    // depends on where its surface falls in the slice: rings on flat ground or water. So a slice
    // holding the visible surface is sampled only in front of it. Last frame's depth (HZB mip 0, ready
    // since the HZB pass, which culling, and so this pass, waits for), reprojected along this ray
    {
        float sliceStart = FroxelToZ(DispatchRaysIndex().z - 1.0f, currentFroxel.resolution.z, asParameters.specialNear, camera.farClip);
        float sliceEnd = FroxelToZ(DispatchRaysIndex().z, currentFroxel.resolution.z, asParameters.specialNear, camera.farClip);
        float4 previousClip = mul(camera.previousViewProj, float4(camera.worldPos.xyz + viewDir * sliceEnd, 1.0f));
        float2 previousUV = float2(previousClip.x, -previousClip.y) / previousClip.w * 0.5f + 0.5f;
        if (previousClip.w > 0.0f && all(previousUV >= 0.0f) && all(previousUV < 1.0f))
        {
            Texture2D<float> previousDepth = ResourceDescriptorHeap[viewContext.HZB];
            float depth = previousDepth.Load(int3(previousUV * viewContext.renderResolution.xy, 0));
            if (depth > 0.0f) // reverse Z: 0 is the sky
            {
                float4 surface = mul(camera.previousViewProj_inv, float4(previousUV.x * 2.0f - 1.0f, 1.0f - previousUV.y * 2.0f, depth, 1.0f));
                float surfaceDistance = dot(surface.xyz / surface.w - camera.worldPos.xyz, viewDir);
                if (surfaceDistance > sliceStart && surfaceDistance < sliceEnd)
                    worldPos = camera.worldPos.xyz + viewDir * lerp(sliceStart, surfaceDistance, 1.0f - jitterZ);
#if DEPTH_CLAMP_BEHIND
                // Slice entirely behind the surface seen along this (jittered) ray: sampled just in
                // front of that surface instead. Otherwise it holds the medium on the far side of the
                // wall/ceiling (sunlit outside), and the column's other rays, the ones that reach farther
                // (a ceiling at grazing angle, a window edge), read it as being in front of them
                // Only the DEPTH_CLAMP_BEHIND slices right behind the surface's: the ones the composite
                // and the column's neighbours read; farther ones keep their own position
                else if (surfaceDistance <= sliceStart && surfaceDistance > FroxelToZ(DispatchRaysIndex().z - 1.0f - DEPTH_CLAMP_BEHIND, currentFroxel.resolution.z, asParameters.specialNear, camera.farClip))
                    worldPos = camera.worldPos.xyz + viewDir * surfaceDistance * (1.0f - 0.01f * (1.0f - jitterZ));
#endif
            }
        }
    }
#endif

    // ---- medium ----
    float3 sigmaS, sigmaT;
    float g;
    FogMedium(worldPos, sigmaS, sigmaT, g);

#if AERIAL_PERSPECTIVE_START > 0
    // The planet atmosphere in front of AERIAL_PERSPECTIVE_START is carried by the froxels, which
    // trace shadows (the aerial perspective volume does not: it lit building interiors with the sun).
    // Rayleigh as isotropic, Mie with its g, mixed into one Henyey-Greenstein lobe like the haze layers
    if (viewContext.atmosphere.transmittanceLUT != HLSL::invalidUINT && length(worldPos - camera.worldPos.xyz) < AERIAL_PERSPECTIVE_START)
    {
        HLSL::Atmosphere atmoMedium = viewContext.atmosphere;
        MediumSample medium = SampleMedium(atmoMedium, AtmospherePosition(atmoMedium, worldPos));
        float gWeighted = g * dot(sigmaS, 1.0f / 3.0f) + atmoMedium.mieAbsorption.w * dot(medium.scatteringMie, 0.001f / 3.0f);
        sigmaS += medium.scattering * 0.001f; // 1/km -> 1/m
        sigmaT += medium.extinction * 0.001f;
        float scatteringMean = dot(sigmaS, 1.0f / 3.0f);
        g = scatteringMean > 0.0f ? clamp(gWeighted / scatteringMean, -0.999f, 0.999f) : g;
    }
#endif

    // Empty froxel: no rays
    if (all(sigmaT <= 0.0f))
    {
        froxelData[DispatchRaysIndex().xyz] = 0.0f;
        return;
    }

    // ---- in-scattered radiance: sum over lights of phase * L ----
    float3 inScattered = 0.0f;
    HLSL::Atmosphere atmo = viewContext.atmosphere;

#if FOG_DEBUG_LIGHTS != 2
    // One RIS-selected analytic light (next event estimation)
    HLSL::Light light;
    float lightWeight;
    if (SampleLightRIS(rngState, worldPos, 0, light, lightWeight, AccelerationStructure))
    {
        float3 incidentVector;
        float lightDistance;
        float irradiance;
        float lightFalloff;
        float2 rand2 = float2(RNG(rngState), RNG(rngState));
        GetLightData(light, worldPos, rand2, rtParameters.enableSoftShadows, incidentVector, lightDistance, irradiance, lightFalloff);
        // Apply distance attenuation once, after RIS selection (see GetLightData comment).
        irradiance *= lightFalloff;
        float3 vectorToLight = -incidentVector;

        float3 lightVisibility = light.castShadow ? CastShadowRay(AccelerationStructure, worldPos, 0, vectorToLight, lightDistance) : float3(1, 1, 1);
        if (any(lightVisibility > 0.0f))
        {
            float3 E = light.color.xyz * irradiance * lightWeight * lightVisibility;
            // Directional lights come from outside the atmosphere
            if (light.type == HLSL::LightType::Directional && atmo.transmittanceLUT != HLSL::invalidUINT)
                E *= SampleTransmittance(atmo, AtmospherePosition(atmo, worldPos), vectorToLight);
            inScattered += E * HenyeyGreensteinPhase(g, dot(vectorToLight, viewDir));
        }
    }

#endif

#if FOG_DEBUG_LIGHTS != 1
    // Indirect light: one direction importance sampled from the phase function, so phase / pdf = 1
    // and the estimate is just the radiance arriving along it: the sky on a miss and, with
    // FOG_INDIRECT_SHARC, the SHARC cache on a hit (radiance leaving the hit surface toward the froxel,
    // updated by lightingProbes; approximate: averaged over a hash grid voxel, view independent, one
    // frame late). Without it a hit counts as black, through the cheaper shadow ray
    {
        float3 dir = SampleHenyeyGreenstein(g, viewDir, float2(RNG(rngState), RNG(rngState)));
#if !FOG_INDIRECT_SHARC
        float3 visibility = CastShadowRay(AccelerationStructure, worldPos, 0, dir, TRACING_DISTANCE);
        if (any(visibility > 0.0f))
            inScattered += Sky(dir) * visibility;
#else
        RayDesc ray;
        ray.Origin = worldPos;
        ray.Direction = dir;
        ray.TMin = 0.01f;
        ray.TMax = TRACING_DISTANCE;
        RayPayload payload; // filled by Miss / ClosestHit
        TraceRay(AccelerationStructure, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        if (payload.hitDistance <= 0.0f) // not payload.Hit(): calling it here crashes dxc (internal compiler error)
            inScattered += Sky(dir);
        else
        {
            SurfaceData s = GetRTSurfaceData(payload.instanceID, payload.primitiveIndex, payload.geometryIndex, payload.barycentrics);
            SharcHitData sharcHitData;
            sharcHitData.positionWorld = worldPos + dir * payload.hitDistance;
            sharcHitData.normalWorld = dot(s.objectNormal, dir) > 0.0f ? -s.objectNormal : s.objectNormal; // same key as raytracing2.hlsl
            sharcHitData.emissive = s.emissiveColor;
            float3 sharcRadiance;
            if (SharcGetCachedRadiance(GetSharcParameters(), sharcHitData, sharcRadiance, false))
                inScattered += sharcRadiance;
        }
#endif
    }
#endif

    // Integrated by AtmosphericScatteringAccumulation over the real slice lengths. sigmaT is stored as
    // its mean: the fog's absorption is rarely chromatic and the alpha channel has room for one value.
    // The color is stored divided by sigmaT (albedo * L): sigmaS * L alone overflows R16F in dense fog,
    // this stays bounded by the incoming radiance whatever the density.
    // Careful: min(NaN, x) returns x, the clamps below turn a NaN into 65000 instead of propagating it
    float sigmaTMean = dot(sigmaT, 1.0f / 3.0f);
    float3 albedoL = sigmaS * inScattered / sigmaTMean;
    froxelData[DispatchRaysIndex().xyz] = float4(min(albedoL, FP16_MAX), min(sigmaTMean, FP16_MAX));
}


[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
    payload.hitDistance = -1.0f;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.instanceID = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.geometryIndex = GeometryIndex();
    payload.barycentrics = attrib.uv;

    uint packedDistance = asuint(RayTCurrent()) & (~0x1u);
    packedDistance |= HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE ? 0x1 : 0x0;
    payload.hitDistance = asfloat(packedDistance);
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    SurfaceData s = GetRTSurfaceData(InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.uv);

    switch (s.shadingDomain)
    {
        case ShadingDomain::AlphaTested:
        case ShadingDomain::TransmissiveAlphaTested:
        {
            if (s.albedo.a < 0.5)
                IgnoreHit();

            break;
        }

        default:
            break;
    }
    // AcceptHit but continue looking for the closest hit
}

// Load-bearing: shadow rays trace with RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, so reaching
// the miss shader is the only signal that the ray was not occluded
[shader("miss")]
void MissShadow(inout ShadowRayPayload payload : SV_RayPayload)
{
    payload.missed = 1;
}

[shader("closesthit")]
void ClosestHitShadow(inout ShadowRayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.visibility = float3(0.0f, 0.0f, 0.0f);
}

[shader("anyhit")]
void AnyHitShadow(inout ShadowRayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    SurfaceData s = GetRTSurfaceData(InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.uv);

    switch (s.shadingDomain)
    {
        case ShadingDomain::AlphaTested:
        case ShadingDomain::TransmissiveAlphaTested:
        {
            if (s.albedo.a < 0.5)
                IgnoreHit();

            break;
        }

        default:
            // Modulate the visiblity by the material's transmission
            payload.visibility *= (1.0f - s.albedo.a) * s.albedo.rgb;
            if (dot(payload.visibility, 0.333f) > 0.001f)
                IgnoreHit();

            break;
    }
    AcceptHitAndEndSearch();
}

#ifndef RAY_DISPATCH
#pragma compute Accumulation AtmosphericScatteringAccumulation

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void AtmosphericScatteringAccumulation(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[asParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[asParameters.currentFroxelIndex];
    HLSL::Froxels historyFroxel = froxels[asParameters.historyFroxelIndex];
    if (any(dtid.xy >= currentFroxel.resolution.xy))
        return;

    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    RWTexture3D<float4> historyFroxelData = ResourceDescriptorHeap[historyFroxel.index];

    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];

    // Input slice z: xyz = sigmaS / sigmaT * in-scattered radiance, w = sigmaT, at froxel coordinate z.
    // Output slice z: xyz = in-scatter S, w = optical depth, integrated from the camera to coordinate z.
    // Slice z covers coordinates (z - 1, z]; FroxelToZ is the distance along the pixel's ray.
    float3 inScatter = 0.0f;
    float opticalDepth = 0.0f;
    float previousDistance = 0.0f;
    for (uint z = 0; z < currentFroxel.resolution.z; z++)
    {
        uint3 fro = uint3(dtid.xy, z);
#if SPATIAL_FILTER
        // 3x3 tent in xy (center 4, edges 2, corners 1) on read only: the history itself stays
        // unfiltered, so unlike a blur written back into it this does not compound every frame
        float4 froData = 0.0f;
        float weightSum = 0.0f;
        [unroll] for (int dy = -1; dy <= 1; dy++)
        [unroll] for (int dx = -1; dx <= 1; dx++)
        {
            int2 xy = int2(dtid.xy) + int2(dx, dy);
            if (any(xy < 0) || any(xy >= int2(currentFroxel.resolution.xy)))
                continue;
            float w = (2 - abs(dx)) * (2 - abs(dy));
            froData += historyFroxelData[uint3(xy, z)] * w;
            weightSum += w;
        }
        froData /= weightSum;
#else
        float4 froData = historyFroxelData[fro];
#endif
        float distance = FroxelToZ(z, currentFroxel.resolution.z, asParameters.specialNear, camera.farClip);
        float segment = distance - previousDistance;
        previousDistance = distance;

        // Energy conserving analytical integration of a constant medium over the segment (Hillaire 2015)
        // (S - S * T) / sigmaT with S = sigmaS * L: the stored color already is S / sigmaT
        float sigmaT = froData.w;
        float segmentTransmittance = exp(-sigmaT * segment);
        inScatter += exp(-opticalDepth) * froData.xyz * (1.0f - segmentTransmittance);
        opticalDepth += sigmaT * segment;

        // Past MAX_OPTICAL_DEPTH nothing gets through anyway (e^-50); beyond R16F's max it would store
        // +inf, and the composite's trilinear filtering of two inf texels is inf - inf = NaN
        froxelData[fro] = float4(min(inScatter, FP16_MAX), min(opticalDepth, MAX_OPTICAL_DEPTH));
    }
}

#pragma compute Reprojection AtmosphericScatteringReprojection

[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 8)]
void AtmosphericScatteringReprojection(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint seed = initRand(dtid.xy * dtid.z);
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[asParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[asParameters.currentFroxelIndex];
    HLSL::Froxels historyFroxel = froxels[asParameters.historyFroxelIndex];
    
    float3 currentWorldPos = FroxelToWorld(dtid.xyz, currentFroxel.resolution.xyz, asParameters.specialNear, camera);
    float3 historyFroxelPos = WorldTohistoryFroxelUVW(currentWorldPos, historyFroxel.resolution.xyz, asParameters.specialNear, camera);
    
    Texture3D<float4> historyFroxelData = ResourceDescriptorHeap[historyFroxel.srvIndex];
    float4 previousData = historyFroxelData.Sample(samplerLinearClamp, historyFroxelPos);
    
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    if (viewContext.frameNumber > 10)
    {
        // A NaN/inf history (never-cleared memory, or an older bug) would stick forever through the
        // blend and spread through the blur every frame: drop it
        bool invalidHistory = any(isnan(previousData)) || any(isinf(previousData));
        float blendFactor = (invalidHistory || any(historyFroxelPos < 0.0) || any(historyFroxelPos > 1.0)) ? 1.0 : TEMPORAL_BLEND;
        froxelData[dtid.xyz] = lerp(previousData, froxelData[dtid.xyz], blendFactor);
    }
}
#endif
