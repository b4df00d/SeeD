#pragma once
//#include "common.hlsl"

#define SHADOW_RAY_INDEX                1

struct [raypayload] RayPayload
{
    float hitDistance       : read(caller) : write(closesthit, miss);
    uint instanceID         : read(caller) : write(closesthit);
    uint primitiveIndex     : read(caller) : write(closesthit);
    uint geometryIndex      : read(caller) : write(closesthit);
    float2 barycentrics     : read(caller) : write(closesthit);

    bool Hit() { return hitDistance > 0.0f; }
    bool IsFrontFacing() { return asuint(hitDistance) & 0x1; }
};

struct [raypayload] ShadowRayPayload
{
    float3 visibility       : read(caller, anyhit) : write(caller, closesthit, anyhit);
    // shadow rays skip the closest-hit shader: reaching the miss shader is the only
    // signal that the ray was NOT occluded
    uint missed             : read(caller) : write(caller, miss);
};

struct Attributes
{
    float2 uv;
};

#define FLT_MAX 3.402823466e+38f
#define M_PI 3.141592653589f
// PIs
#ifndef PI
#define PI 3.141592653589f
#endif

#ifndef TWO_PI
#define TWO_PI (2.0f * PI)
#endif

#ifndef ONE_OVER_PI
#define ONE_OVER_PI (1.0f / PI)
#endif

#ifndef ONE_OVER_TWO_PI
#define ONE_OVER_TWO_PI (1.0f / TWO_PI)
#endif

float square(float value)
{
    return value*value;
}

float luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 GetPerpendicularVector(float3 u)
{
    float3 a = abs(u);
    uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
    uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
    uint zm = 1 ^ (xm | ym);
    return cross(u, float3(xm, ym, zm));
}

// Clever offset_ray function from Ray Tracing Gems chapter 6
// Offsets the ray origin from current position p, along normal n (which must be geometric normal)
// so that no self-intersection can occur.
float3 OffsetRay(const float3 p, const float3 n)
{
    static const float origin = 1.0f / 32.0f;
    static const float float_scale = 1.0f / 65536.0f;
    static const float int_scale = 256.0f;

    int3 of_i = int3(int_scale * n.x, int_scale * n.y, int_scale * n.z);

    float3 p_i = float3(asfloat(asint(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)), asfloat(asint(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)), asfloat(asint(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return float3(abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x, abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y, abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

// Bounce heatmap visualization: https://developer.nvidia.com/blog/profiling-dxr-shaders-with-timer-instrumentation/
inline float3 Temperature(float t)
{
    const float3 c[10] = { float3(0.0f / 255.0f, 2.0f / 255.0f, 91.0f / 255.0f),    float3(0.0f / 255.0f, 108.0f / 255.0f, 251.0f / 255.0f),
                           float3(0.0f / 255.0f, 221.0f / 255.0f, 221.0f / 255.0f), float3(51.0f / 255.0f, 221.0f / 255.0f, 0.0f / 255.0f),
                           float3(255.0f / 255.0f, 252.0f / 255.0f, 0.0f / 255.0f), float3(255.0f / 255.0f, 180.0f / 255.0f, 0.0f / 255.0f),
                           float3(255.0f / 255.0f, 104.0f / 255.0f, 0.0f / 255.0f), float3(226.0f / 255.0f, 22.0f / 255.0f, 0.0f / 255.0f),
                           float3(191.0f / 255.0f, 0.0f / 255.0f, 83.0f / 255.0f),  float3(145.0f / 255.0f, 0.0f / 255.0f, 65.0f / 255.0f) };

    const float s = t * 10.0f;

    const int cur = int(s) <= 9 ? int(s) : 9;
    const int prv = cur >= 1 ? cur - 1 : 0;
    const int nxt = cur < 9 ? cur + 1 : 9;

    const float blur = 0.8f;

    const float wc = smoothstep(float(cur) - blur, float(cur) + blur, s) * (1.0f - smoothstep(float(cur + 1) - blur, float(cur + 1) + blur, s));
    const float wp = 1.0f - smoothstep(float(cur) - blur, float(cur) + blur, s);
    const float wn = smoothstep(float(cur + 1) - blur, float(cur + 1) + blur, s);

    const float3 r = wc * c[cur] + wp * c[prv] + wn * c[nxt];
    return float3(clamp(r.x, 0.0f, 1.0f), clamp(r.y, 0.0f, 1.0f), clamp(r.z, 0.0f, 1.0f));
}

inline float3 BounceHeatmap(uint bounce)
{
    switch (bounce)
    {
    case 0:
        return float3(0.0f, 0.0f, 1.0f);
    case 1:
        return float3(0.0f, 1.0f, 0.0f);
    default:
        return float3(1.0f, 0.0f, 0.0f);
    }
}

// Decodes light vector and distance from Light structure based on the light type.
// 'irradiance' is the *base* (distance-independent) radiometric weight and is what the RIS
// resampling target PDF should use. 'lightFalloff' carries the 1/d^2 (and range) distance
// attenuation and must be multiplied into the final shading AFTER a light has been selected.
// Keeping the falloff out of 'irradiance' is deliberate: if it were baked in here it would end
// up in both the RIS target PDF and the final term and cancel exactly (f / pHat == 1), so the
// per-light distance attenuation would be invisible in the image.
void GetLightData(HLSL::Light light, float3 surfacePos, float2 rand2, bool enableSoftShadows, out float3 incidentVector, out float lightDistance, out float irradiance, out float lightFalloff)
{
    incidentVector = 0;
    float halfAngularSize = 0;
    irradiance = 0;
    lightDistance = 0;
    lightFalloff = 1.0f;

    if (light.type == HLSL::LightType::Directional)
    {
        if (enableSoftShadows)
        {
            float3 bitangent = normalize(GetPerpendicularVector(light.dir.xyz));
            float3 tangent = cross(bitangent, light.dir.xyz);

            float angle = rand2.x * 2.0f * M_PI;
            float distance = sqrt(rand2.y);

            incidentVector = light.dir.xyz + (bitangent * sin(angle) + tangent * cos(angle)) * tan(light.size) * distance;
            incidentVector = normalize(incidentVector);
        }
        else
            incidentVector = normalize(light.dir.xyz);

        lightDistance = FLT_MAX;
        halfAngularSize = light.size * 0.5f;
        irradiance = light.color.w;
    }
    else if (light.type == HLSL::LightType::Spot || light.type == HLSL::LightType::Point)
    {
        float3 lightToSurface = surfacePos - light.pos.xyz;
        float distance = sqrt(dot(lightToSurface, lightToSurface));
        float rDistance = 1.0f / distance;
        incidentVector = lightToSurface * rDistance;
        lightDistance = distance;

        // Soft shadows: aim the shadow ray at a random point on the emitter's disk (radius = size)
        // facing the surface, instead of always at its center. Falloff/cone below keep using the
        // center so only visibility gets the penumbra.
        if (enableSoftShadows && light.size > 0)
        {
            float3 bitangent = normalize(GetPerpendicularVector(incidentVector));
            float3 tangent = cross(bitangent, incidentVector);
            float angle = rand2.x * 2.0f * M_PI;
            float radius = sqrt(rand2.y) * min(light.size, distance * 0.99f);
            float3 samplePos = light.pos.xyz + (bitangent * sin(angle) + tangent * cos(angle)) * radius;
            float3 sampleToSurface = surfacePos - samplePos;
            lightDistance = length(sampleToSurface);
            incidentVector = sampleToSurface / lightDistance;
        }

        // Irradiance from a point light of radiant power P watts is P / (4*pi*d^2).
        // This used to be 10/d^2 clamped at 100 -- off by 4*pi*10 and discontinuous.
        // max(d^2, size^2) keeps it finite inside the light's own radius instead of
        // relying on a clamp to hide the singularity.
        float softenedDistanceSquared = max(dot(lightToSurface, lightToSurface), square(light.size));
        float attenuation = 1.0f / (4.0f * M_PI * softenedDistanceSquared);
        if (light.range > 0)
        {
            float d2 = square(distance / light.range);
            attenuation *= square(saturate(1.0f - square(d2)));   // (1 - (d/range)^4)^2
        }

        float spotlight = 1.0f;
        if (light.type == HLSL::LightType::Spot)
        {
            float LdotD = dot(lightToSurface * rDistance, light.dir.xyz);
            float directionAngle = acos(LdotD);
            // light.angle is the OUTER cone half-angle in radians. It used to be
            // compared in radians while the editor authored degrees, so every spot
            // was effectively fully lit. SeeD has no separate inner cone yet, so the
            // edge softens over the outer fifth of the cone.
            spotlight = 1.0f - smoothstep(light.angle * 0.8f, light.angle, directionAngle);
            //if (spotlight == 0) return;
        }

        irradiance = light.color.w;

        // size is the emitter's physical radius, used for soft shadows only. It used
        // to also rewrite irradiance as a solid-angle integral, which already carries
        // its own 1/d^2 -- so with the attenuation above it counted distance twice and
        // a light with any radius fell off as 1/d^4. A sphere light of the same power
        // produces the same irradiance as a point one for d > r, so there is nothing
        // to substitute here.
        if (light.size > 0)
            halfAngularSize = atan(min(light.size * rDistance, 1.0f));

        // Spotlight cone is part of the base weight (it is angular, not distance based), but the
        // distance attenuation is returned separately so callers apply it once after RIS selection.
        irradiance *= spotlight;
        lightFalloff = attenuation;
    }
    else
        return;
}

// Casts a shadow ray and returns true if light is not occluded ie. it hits nothing
// Note that we use dedicated hit group with simpler shaders for shadow rays
float3 CastShadowRay(RaytracingAccelerationStructure accelerationStructure, float3 hitPosition, float3 surfaceNormal, float3 directionToLight, float tracingDistance)
{
    // No need to cast shadow rays for back-facing primitives if there is no transmission
    if (!rtParameters.enableTransmission && dot(surfaceNormal, directionToLight) < 0)
        return float3(0, 0, 0);

    RayDesc ray;
    ray.Origin = OffsetRay(hitPosition, surfaceNormal);
    ray.Direction = directionToLight;
    ray.TMin = 0.0f;
    ray.TMax = tracingDistance;

    ShadowRayPayload payload;
    payload.visibility = float3(1.0f, 1.0f, 1.0f);
    payload.missed = 0;

    // Cull backfaces : the ray origin can be inside the geometry when reconstructed from the gbuffer,
    // without culling the ray hits the backface of its own surface and wrongly shadows the pixel
    uint rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
                  | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
                  | ((!rtParameters.enableBackFaceCull) ? RAY_FLAG_NONE : RAY_FLAG_CULL_BACK_FACING_TRIANGLES);

#if DISABLE_BACK_FACE_CULLING
    rayFlags &= (~RAY_FLAG_CULL_BACK_FACING_TRIANGLES);
#endif // DISABLE_BACK_FACE_CULLING
    //rayFlags &= (~RAY_FLAG_FORCE_OPAQUE);
    TraceRay(accelerationStructure, rayFlags, 0xFF, SHADOW_RAY_INDEX, 0, SHADOW_RAY_INDEX, ray, payload);

    return payload.missed ? payload.visibility : float3(0, 0, 0);
}

// Samples a random light from the pool of all lights using RIS (Resampled Importance Sampling)
bool SampleLightRIS(inout uint rngState, float3 hitPosition, float3 surfaceNormal, inout HLSL::Light selectedSample, out float lightSampleWeight, RaytracingAccelerationStructure accelerationStructure)
{
    lightSampleWeight = 1.0f;
    if (commonResourcesIndices.lightCount == 0)
        return false;
    
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];

    selectedSample = lights[0];
    if (commonResourcesIndices.lightCount == 1)
        return true;

    float totalWeights = 0.0f;
    float samplePdfG = 0.0f;

    const uint candidateMax = min(commonResourcesIndices.lightCount, RIS_CANDIDATES_LIGHTS);
    for (int i = 0; i < candidateMax; i++)
    {
        uint randomLightIndex = min(commonResourcesIndices.lightCount - 1, uint((RNG(rngState) * commonResourcesIndices.lightCount + viewContext.frameNumber) % commonResourcesIndices.lightCount));
        //randomLightIndex = (i + viewContext.frameNumber) % commonResourcesIndices.lightCount;
        HLSL::Light candidate = lights[randomLightIndex];

        // PDF of uniform distribution is (1 / light count). Reciprocal of that PDF (simply a light count) is a weight of this sample
        float candidateWeight = float(commonResourcesIndices.lightCount);

        {
            float3 lightVector;
            float lightDistance;
            float irradiance;
            float lightFalloff;
            float2 rand2 = float2(RNG(rngState), RNG(rngState));
            GetLightData(candidate, hitPosition, rand2, rtParameters.enableSoftShadows, lightVector, lightDistance, irradiance, lightFalloff);

#if SHADOW_RAY_IN_RIS
            // Casting a shadow ray for all candidates here is expensive, but can significantly decrease noise
            if (candidate.castShadow && any(CastShadowRay(accelerationStructure, hitPosition, surfaceNormal, lightVector, lightDistance) > 0.0f))
            {
                continue;
            }
#endif

            // The target PDF has to track what the light actually contributes here,
            // distance falloff included. Leaving falloff out (as this did) is still
            // unbiased, but it makes pHat a bad match for f: with a 5 W/m^2 sun and a
            // 100 W point light, the point was picked ~95% of the time even across
            // ground where it delivers almost nothing, so the sun arrived as a rare
            // sample carrying a ~21x weight. Correct on average, far too spiky to
            // resolve at one sample per pixel -- it read several times too bright.
            float candidatePdfG = irradiance * lightFalloff;
            const float candidateRISWeight = candidatePdfG * candidateWeight;

            totalWeights += candidateRISWeight;
            // RNG, not Rand: Rand(rngState) silently resolved to Rand(float), a sin() hash of a
            // ~1e9 value that is nowhere near uniform. The selection then ignored the weights while
            // lightSampleWeight still assumed them, so a point light came out at a flat brightness
            // anywhere its falloff was non-zero (hard cut at range, infinite reach with range 0).
            if (RNG(rngState) * totalWeights < candidateRISWeight)
            {
                selectedSample = candidate;
                samplePdfG = candidatePdfG;
            }
        }
    }

    if (totalWeights == 0.0f)
    {
        return false;
    }
    else
    {
        lightSampleWeight = (totalWeights / float(candidateMax)) / samplePdfG;

        return true;
    }
}


// -------------------------------------------------------------------------
//    Alpha test
// -------------------------------------------------------------------------
// Only TLAS instances of alpha-tested materials (alphaCutoff != 0) run any-hit shaders, every other
// one is FORCE_OPAQUE (culling.hlsl). Coverage below 0.5 lets the ray through.

// any-hit of the radiance rays: keep looking for the closest hit
void AlphaTestAnyHit(Attributes attrib)
{
    if (GetRTOpacity(InstanceID(), PrimitiveIndex(), attrib.uv) < 0.5f)
        IgnoreHit();
}

// any-hit of the shadow rays (closest hit skipped): any covered hit occludes
void AlphaTestAnyHitShadow(Attributes attrib)
{
    if (GetRTOpacity(InstanceID(), PrimitiveIndex(), attrib.uv) < 0.5f)
        IgnoreHit();
    else
        AcceptHitAndEndSearch();
}

