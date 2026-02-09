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

// -------------------------------------------------------------------------
//    Quaternion rotations
// -------------------------------------------------------------------------

// Calculates rotation quaternion from input vector to the vector (0, 0, 1)
// Input vector must be normalized!
float4 getRotationToZAxis(float3 input)
{

    // Handle special case when input is exact or near opposite of (0, 0, 1)
    if (input.z < -0.99999f)
    {
        return float4(1.0f, 0.0f, 0.0f, 0.0f);
    }

    return normalize(float4(input.y, -input.x, 0.0f, 1.0f + input.z));
}

// Calculates rotation quaternion from vector (0, 0, 1) to the input vector
// Input vector must be normalized!
float4 getRotationFromZAxis(float3 input)
{

    // Handle special case when input is exact or near opposite of (0, 0, 1)
    if (input.z < -0.99999f)
    {
        return float4(1.0f, 0.0f, 0.0f, 0.0f);
    }

    return normalize(float4(-input.y, input.x, 0.0f, 1.0f + input.z));
}

// Returns the quaternion with inverted rotation
float4 invertRotation(float4 q)
{
    return float4(-q.x, -q.y, -q.z, q.w);
}

// Optimized point rotation using quaternion
// Source: https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
float3 rotatePoint(float4 q, float3 v)
{
    const float3 qAxis = float3(q.x, q.y, q.z);
    return 2.0f * dot(qAxis, v) * qAxis + (q.w * q.w - dot(qAxis, qAxis)) * v + 2.0f * q.w * cross(qAxis, v);
}

// Jenkins's "one at a time" hash function
uint JenkinsHash(uint x)
{
    x += x << 10;
    x ^= x >> 6;
    x += x << 3;
    x ^= x >> 11;
    x += x << 15;
    return x;
}

// Maps integers to colors using the hash function (generates pseudo-random colors)
float3 HashAndColor(int i)
{
    uint hash = JenkinsHash(i);
    float r = ((hash >> 0) & 0xFF) / 255.0f;
    float g = ((hash >> 8) & 0xFF) / 255.0f;
    float b = ((hash >> 16) & 0xFF) / 255.0f;
    return float3(r, g, b);
}

uint InitRNG(uint2 pixel, uint2 resolution, uint frame)
{
    uint rngState = dot(pixel, uint2(1, resolution.x)) ^ JenkinsHash(frame);
    return JenkinsHash(rngState);
}

float UintToFloat(uint x)
{
    return asfloat(0x3f800000 | (x >> 9)) - 1.f;
}

uint XorShift(inout uint rngState)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

float RNG(inout uint rngState)
{
    return UintToFloat(XorShift(rngState));
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

// Decodes light vector and distance from Light structure based on the light type
void GetLightData(HLSL::Light light, float3 surfacePos, float2 rand2, bool enableSoftShadows, out float3 incidentVector, out float lightDistance, out float irradiance)
{
    incidentVector = 0;
    float halfAngularSize = 0;
    irradiance = 0;
    lightDistance = 0;

    if (light.type == HLSL::LightType::Directional)
    {
        if (enableSoftShadows)
        {
            float3 bitangent = normalize(GetPerpendicularVector(light.dir.xyz));
            float3 tangent = cross(bitangent, light.dir.xyz);

            float angle = rand2.x * 2.0f * M_PI;
            float distance = sqrt(rand2.y);

            incidentVector = light.dir.xyz + (bitangent * sin(angle) + tangent * cos(angle)) * tan(light.size * 0.5f) * distance;
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
        lightDistance = length(lightToSurface);

        float attenuation = 1.0f;
        if (light.size > 0)
        {
            attenuation = square(saturate(1.0f - square(square(distance * light.size))));

            if (attenuation == 0)
                return;
        }

        float spotlight = 1.0f;
        if (light.type == HLSL::LightType::Spot)
        {
            float LdotD = dot(incidentVector, light.dir.xyz);
            float directionAngle = acos(LdotD);
            spotlight = 1.0f - smoothstep(light.angle, light.angle*2, directionAngle);
            if (spotlight == 0)
                return;
        }

        if (light.size > 0)
        {
            halfAngularSize = atan(min(light.size * rDistance, 1.0f));

            // A good enough approximation for 2 * (1 - cos(halfAngularSize)), numerically more accurate for small angular sizes
            float solidAngleOverPi = square(halfAngularSize);
            float radianceTimesPi = light.color.w / square(light.size);

            irradiance = radianceTimesPi * solidAngleOverPi;
        }
        else
            irradiance = light.color.w * square(rDistance);

        irradiance *= spotlight * attenuation;
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

    uint rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    TraceRay(accelerationStructure, rayFlags, 0xFF, SHADOW_RAY_INDEX, 0, SHADOW_RAY_INDEX, ray, payload);

    return payload.visibility;
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
        uint randomLightIndex = min(commonResourcesIndices.lightCount - 1, uint(RNG(rngState) * commonResourcesIndices.lightCount));
        HLSL::Light candidate = lights[randomLightIndex];

        // PDF of uniform distribution is (1 / light count). Reciprocal of that PDF (simply a light count) is a weight of this sample
        float candidateWeight = float(commonResourcesIndices.lightCount);

        {
            float3 lightVector;
            float lightDistance;
            float irradiance;
            float2 rand2 = float2(RNG(rngState), RNG(rngState));
            GetLightData(candidate, hitPosition, rand2, rtParameters.enableSoftShadows, lightVector, lightDistance, irradiance);

#if SHADOW_RAY_IN_RIS
            // Casting a shadow ray for all candidates here is expensive, but can significantly decrease noise
            if (any(CastShadowRay(accelerationStructure, hitPosition, surfaceNormal, lightVector, lightDistance) > 0.0f))
                continue;
#endif

            float candidatePdfG = irradiance;
            const float candidateRISWeight = candidatePdfG * candidateWeight;

            totalWeights += candidateRISWeight;
            if (Rand(rngState) < (candidateRISWeight / totalWeights))
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


// Specifies minimal reflectance for dielectrics (when metalness is zero)
// Nothing has lower reflectance than 2%, but we use 4% to have consistent results with UE4, Frostbite, et al.
#define MIN_DIELECTRICS_F0 0.04f
// BRDF types
#define DIFFUSE_TYPE 1
#define SPECULAR_TYPE 2
#define TRANSMISSIVE_TYPE 3

// Schlick's approximation to Fresnel term
// f90 should be 1.0, except for the trick used by Schuler (see 'shadowedF90' function)
float3 evalFresnelSchlick(float3 f0, float f90, float NdotS)
{
    return f0 + (f90 - f0) * pow(1.0f - NdotS, 5.0f);
}

// Schlick's approximation to Fresnel term calculated using spherical gaussian approximation
// Source: https://seblagarde.wordpress.com/2012/06/03/spherical-gaussien-approximation-for-blinn-phong-phong-and-fresnel/ by Lagarde
float3 evalFresnelSchlickSphericalGaussian(float3 f0, float f90, float NdotV)
{
    return f0 + (f90 - f0) * exp2((-5.55473f * NdotV - 6.983146f) * NdotV);
}

float3 evalFresnel(float3 f0, float f90, float NdotS)
{
    // Default is Schlick's approximation
    return evalFresnelSchlick(f0, f90, NdotS);
}

// Attenuates F90 for very low F0 values
// Source: "An efficient and Physically Plausible Real-Time Shading Model" in ShaderX7 by Schuler
// Also see section "Overbright highlights" in Hoffman's 2010 "Crafting Physically Motivated Shading Models for Game Development" for discussion
// IMPORTANT: Note that when F0 is calculated using metalness, it's value is never less than MIN_DIELECTRICS_F0, and therefore,
// this adjustment has no effect. To be effective, F0 must be authored separately, or calculated in different way. See main text for discussion.
float shadowedF90(float3 F0)
{
    // This scaler value is somewhat arbitrary, Schuler used 60 in his article. In here, we derive it from MIN_DIELECTRICS_F0 so
    // that it takes effect for any reflectance lower than least reflective dielectrics
    // const float t = 60.0f;
    const float t = (1.0f / MIN_DIELECTRICS_F0);
    return min(1.0f, t * luminance(F0));
}

// Calculates probability of selecting BRDF (specular or diffuse) using the approximate Fresnel term
float GetSpecularBrdfProbability(SurfaceData s, float3 viewVector, float3 shadingNormal)
{
#if ENABLE_SPECULAR_LOBE
    // Fast path for mirrors
    if (s.metalness == 1.0f && s.roughness == 0.0f)
        return 1.0f;

    // Evaluate Fresnel term using the shading normal
    // Note: we use the shading normal instead of the microfacet normal (half-vector) for Fresnel term here. That's suboptimal for rough surfaces at grazing angles, but half-vector is yet unknown at this point
    float specularF0 = 0.04;//luminance(s.specularF0);
    float diffuseReflectance = luminance(s.albedo.xyz);

    float fresnel = saturate(luminance(evalFresnel(specularF0, shadowedF90(specularF0), max(0.0f, dot(viewVector, shadingNormal)))));

    // Approximate relative contribution of BRDFs using the Fresnel term
    float specular = fresnel;
    float diffuse = diffuseReflectance * (1.0f - fresnel); //< If diffuse term is weighted by Fresnel, apply it here as well

    // Return probability of selecting specular BRDF over diffuse BRDF
    float probability = (specular / max(0.0001f, (specular + diffuse)));

    // Clamp probability to avoid undersampling of less prominent BRDF
    return clamp(probability, 0.1f, 0.9f);
#else // !ENABLE_SPECULAR_LOBE
    return 0.0f;
#endif // !ENABLE_SPECULAR_LOBE
}

// Data needed to evaluate BRDF (surface and material properties at given point + configuration of light and normal vectors)
struct BrdfData
{
    // Material properties
    float3 specularF0;
    float3 diffuseReflectance;

    // Roughnesses
    float roughness; //< perceptively linear roughness (artist's input)
    float alpha; //< linear roughness - often 'alpha' in specular BRDF equations
    float alphaSquared; //< alpha squared - pre-calculated value commonly used in BRDF equations

    // Commonly used terms for BRDF evaluation
    float3 F; //< Fresnel term

    // Vectors
    float3 V; //< Direction to viewer (or opposite direction of incident ray)
    float3 N; //< Shading normal
    float3 H; //< Half vector (microfacet normal)
    float3 L; //< Direction to light (or direction of reflecting ray)

    float NdotL;
    float NdotV;

    float LdotH;
    float NdotH;
    float VdotH;

    // True when V/L is backfacing wrt. shading normal N
    bool Vbackfacing;
    bool Lbackfacing;
};

// Calculates all the BRDF fields that depend on the L (light direction) vector
// Expects N and V to be set correctly
void setBRDFDataLightDirection(inout BrdfData data, float3 L)
{
    data.H = normalize(L + data.V);
    data.L = L;

    float NdotL = dot(data.N, data.L);
    data.Lbackfacing = (NdotL <= 0.0f);

    // Clamp NdotS to prevent numerical instability. Assume vectors below the hemisphere will be filtered using 'Vbackfacing' and 'Lbackfacing' flags
    data.NdotL = min(max(0.00001f, NdotL), 1.0f);

    data.LdotH = saturate(dot(data.L, data.H));
    data.NdotH = saturate(dot(data.N, data.H));
    data.VdotH = saturate(dot(data.V, data.H));
}

float3 baseColorToSpecularF0(float3 baseColor, float metalness)
{
    return lerp(float3(MIN_DIELECTRICS_F0, MIN_DIELECTRICS_F0, MIN_DIELECTRICS_F0), baseColor, metalness);
}

float3 baseColorToDiffuseReflectance(float3 baseColor, float metalness)
{
    return baseColor * (1.0f - metalness);
}

// Precalculates commonly used terms in BRDF evaluation
// Clamps around dot products prevent NaNs and ensure numerical stability, but make sure to
// correctly ignore rays outside of the sampling hemisphere, by using 'Vbackfacing' and 'Lbackfacing' flags
BrdfData prepareBRDFData(float3 N, float3 L, float3 V, SurfaceData material)
{
    BrdfData data;

    // Evaluate VNHL vectors
    data.V = V;
    data.N = N;
    setBRDFDataLightDirection(data, L);

    float NdotV = dot(N, V);
    data.Vbackfacing = (NdotV <= 0.0f);

    // Clamp NdotS to prevent numerical instability. Assume vectors below the hemisphere will be filtered using 'Vbackfacing' and 'Lbackfacing' flags
    data.NdotV = min(max(0.00001f, NdotV), 1.0f);

    // Unpack material properties
    data.specularF0 = baseColorToSpecularF0(material.albedo.xyz, material.metalness);
    data.diffuseReflectance = baseColorToDiffuseReflectance(material.albedo.xyz, material.metalness);

    // Unpack 'perceptively linear' -> 'linear' -> 'squared' roughness
    data.roughness = material.roughness;
    data.alpha = material.roughness * material.roughness;
    data.alphaSquared = data.alpha * data.alpha;

    // Pre-calculate some more BRDF terms
    data.F = evalFresnel(data.specularF0, shadowedF90(data.specularF0), data.LdotH);

    return data;
}

float GGX_D(float alphaSquared, float NdotH)
{
    float b = ((alphaSquared - 1.0f) * saturate(NdotH * NdotH) + 1.0f);
    b = max(b, 0.0000001f);
    return alphaSquared / (PI * b * b);
}

// Smith G1 term (masking function) optimized for GGX distribution (by substituting G_Lambda_GGX into G1)
float Smith_G1_GGX(float a)
{
    float a2 = a * a;
    return 2.0f / (sqrt((a2 + 1.0f) / a2) + 1.0f);
}

// Smith G1 term (masking function) further optimized for GGX distribution (by substituting G_a into G1_GGX)
float Smith_G1_GGX(float alpha, float NdotS, float alphaSquared, float NdotSSquared)
{
    return 2.0f / (sqrt(((alphaSquared * (1.0f - NdotSSquared)) + NdotSSquared) / NdotSSquared) + 1.0f);
}

// Smith G2 term (masking-shadowing function) for GGX distribution
// Separable version assuming independent (uncorrelated) masking and shadowing - optimized by substituing G_Lambda for G_Lambda_GGX and
// dividing by (4 * NdotL * NdotV) to cancel out these terms in specular BRDF denominator
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Separable_GGX_Lagarde(float alphaSquared, float NdotL, float NdotV)
{
    float a = NdotV + sqrt(alphaSquared + NdotV * (NdotV - alphaSquared * NdotV));
    float b = NdotL + sqrt(alphaSquared + NdotL * (NdotL - alphaSquared * NdotL));
    return 1.0f / (a * b);
}

// Evaluates G2 for selected configuration (GGX/Beckmann, optimized/non-optimized, separable/height-correlated)
// Note that some paths aren't optimized too much...
// Also note that when USE_OPTIMIZED_G2 is specified, returned value will be: G2 / (4 * NdotL * NdotV) if GG-X is selected
float Smith_G2(float alpha, float alphaSquared, float NdotL, float NdotV)
{
    return Smith_G2_Separable_GGX_Lagarde(alphaSquared, NdotL, NdotV);
}

// -------------------------------------------------------------------------
//    Microfacet model
// -------------------------------------------------------------------------

// Samples a microfacet normal for the GGX distribution using VNDF method.
// Source: "Sampling the GGX Distribution of Visible Normals" by Heitz
// See also https://hal.inria.fr/hal-00996995v1/document and http://jcgt.org/published/0007/04/01/
// Random variables 'u' must be in <0;1) interval
// PDF is 'G1(NdotV) * D'
float3 sampleGGXVNDF(float3 Ve, float2 alpha2D, float2 u)
{

    // Section 3.2: transforming the view direction to the hemisphere configuration
    float3 Vh = normalize(float3(alpha2D.x * Ve.x, alpha2D.y * Ve.y, Ve.z));

    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0f ? float3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
    float3 T2 = cross(Vh, T1);

    // Section 4.2: parameterization of the projected area
    float r = sqrt(u.x);
    float phi = TWO_PI * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5f * (1.0f + Vh.z);
    t2 = lerp(sqrt(1.0f - t1 * t1), t2, s);

    // Section 4.3: reprojection onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

    // Section 3.4: transforming the normal back to the ellipsoid configuration
    return normalize(float3(alpha2D.x * Nh.x, alpha2D.y * Nh.y, max(0.0f, Nh.z)));
}

// PDF of sampling a reflection vector L using 'sampleGGXVNDF'.
// Note that PDF of sampling given microfacet normal is (G1 * D) when vectors are in local space (in the hemisphere around shading normal).
// Remaining terms (1.0f / (4.0f * NdotV)) are specific for reflection case, and come from multiplying PDF by jacobian of reflection operator
float sampleGGXVNDFReflectionPdf(float alpha, float alphaSquared, float NdotH, float NdotV, float LdotH)
{
    NdotH = max(0.00001f, NdotH);
    NdotV = max(0.00001f, NdotV);
    return (GGX_D(max(0.00001f, alphaSquared), NdotH) * Smith_G1_GGX(alpha, NdotV, alphaSquared, NdotV * NdotV)) / (4.0f * NdotV);
}

// Weight for the reflection ray sampled from GGX distribution using VNDF method
float specularSampleWeightGGXVNDF(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH)
{
    return Smith_G1_GGX(alpha, NdotL, alphaSquared, NdotL * NdotL);
}

// Samples a reflection ray from the rough surface using selected microfacet distribution and sampling method
// Resulting weight includes multiplication by cosine (NdotL) term
float3 sampleSpecularMicrofacet(float3 Vlocal, float alpha, float alphaSquared, float3 specularF0, float2 u, const float ior, out float3 weight)
{

    // Sample a microfacet normal (H) in local space
    float3 Hlocal;
    if (alpha == 0.0f)
    {
        // Fast path for zero roughness (perfect reflection), also prevents NaNs appearing due to divisions by zeroes
        Hlocal = float3(0.0f, 0.0f, 1.0f);
    }
    else
    {
        // For non-zero roughness, this calls VNDF sampling for GG-X distribution or Walter's sampling for Beckmann distribution
        Hlocal = sampleGGXVNDF(Vlocal, float2(alpha, alpha), u);
    }

    // Reflect view direction to obtain light vector
    // float3 Llocal = reflect(-Vlocal, Hlocal);
    float3 Llocal;
    if (ior != 0.0f)
    {
        // Refract the view direction to obtain light vector
        Llocal = refract(-Vlocal, Hlocal, ior);
    }
    else
    {
        // Reflect view direction to obtain light vector
        Llocal = reflect(-Vlocal, Hlocal);
    }

    // Note: HdotL is same as HdotV here
    // Clamp dot products here to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
    float HdotL = max(0.00001f, min(1.0f, dot(Hlocal, Llocal)));
    const float3 Nlocal = float3(0.0f, 0.0f, 1.0f);
    float NdotL = max(0.00001f, min(1.0f, dot(Nlocal, Llocal)));
    float NdotV = max(0.00001f, min(1.0f, dot(Nlocal, Vlocal)));
    float NdotH = max(0.00001f, min(1.0f, dot(Nlocal, Hlocal)));
    float3 F = evalFresnel(specularF0, shadowedF90(specularF0), HdotL);

    // Calculate weight of the sample specific for selected sampling method
    // (this is microfacet BRDF divided by PDF of sampling method - notice how most terms cancel out)
    weight = F * specularSampleWeightGGXVNDF(alpha, alphaSquared, NdotL, NdotV, HdotL, NdotH);

    return Llocal;
}

// Evaluates microfacet specular BRDF
float3 evalMicrofacet(const BrdfData data)
{

    float D = GGX_D(max(0.00001f, data.alphaSquared), data.NdotH);
    float G2 = Smith_G2(data.alpha, data.alphaSquared, data.NdotL, data.NdotV);
    // float3 F = evalFresnel(data.specularF0, shadowedF90(data.specularF0), data.VdotH); //< Unused, F is precomputed already
    
    return data.F * (G2 * D * data.NdotL);
}

// Frostbite's version of Disney diffuse with energy normalization.
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
float frostbiteDisneyDiffuse(const BrdfData data)
{
    float energyBias = 0.5f * data.roughness;
    float energyFactor = lerp(1.0f, 1.0f / 1.51f, data.roughness);

    float FD90MinusOne = energyBias + 2.0 * data.LdotH * data.LdotH * data.roughness - 1.0f;

    float FDL = 1.0f + (FD90MinusOne * pow(1.0f - data.NdotL, 5.0f));
    float FDV = 1.0f + (FD90MinusOne * pow(1.0f - data.NdotV, 5.0f));

    return FDL * FDV * energyFactor;
}

float3 evalFrostbiteDisneyDiffuse(const BrdfData data)
{
    return data.diffuseReflectance * (frostbiteDisneyDiffuse(data) * ONE_OVER_PI * data.NdotL);
}

// This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
float3 evalCombinedBRDF(float3 N, float3 L, float3 V, SurfaceData material)
{

    // Prepare data needed for BRDF evaluation - unpack material properties and evaluate commonly used terms (e.g. Fresnel, NdotL, ...)
    const BrdfData data = prepareBRDFData(N, L, V, material);

    // Ignore V and L rays "below" the hemisphere
    if (data.Vbackfacing || data.Lbackfacing)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    // Eval specular and diffuse BRDFs
    float3 specular = evalMicrofacet(data);
    float3 diffuse = evalFrostbiteDisneyDiffuse(data);

    // Combine specular and diffuse layers
    return diffuse + specular;
}

// Samples a direction within a hemisphere oriented along +Z axis with a cosine-weighted distribution
// Source: "Sampling Transformations Zoo" in Ray Tracing Gems by Shirley et al.
float3 sampleHemisphere(float2 u, out float pdf)
{

    float a = sqrt(u.x);
    float b = TWO_PI * u.y;

    float3 result = float3(a * cos(b), a * sin(b), sqrt(1.0f - u.x));

    pdf = result.z * ONE_OVER_PI;

    return result;
}

float3 sampleHemisphere(float2 u)
{
    float pdf;
    return sampleHemisphere(u, pdf);
}

// For sampling of all our diffuse BRDFs we use cosine-weighted hemisphere sampling, with PDF equal to (NdotL/PI)
float diffusePdf(float NdotL)
{
    return NdotL * ONE_OVER_PI;
}

bool evalIndirectCombinedBRDF(float2 u,
                              float3 shadingNormal,
                              float3 geometryNormal,
                              float3 V,
                              SurfaceData material,
                              const int brdfType,
                              const float refractiveIndex,
                              out float3 rayDirection,
                              out float3 sampleWeight,
                              out float pdf)
{
    // Ignore incident ray coming from "below" the hemisphere
    rayDirection = 0;
    pdf = 0.f;
    sampleWeight = 0;
    if (dot(shadingNormal, V) <= 0.0f)
    {
        return false;
    }

    // Transform view direction into local space of our sampling routines
    // (local space is oriented so that its positive Z axis points along the shading normal)
    float4 qRotationToZ = getRotationToZAxis(shadingNormal);
    float3 Vlocal = rotatePoint(qRotationToZ, V);
    const float3 Nlocal = float3(0.0f, 0.0f, 1.0f);

    float3 rayDirectionLocal = float3(0.0f, 0.0f, 0.0f);

    // Transmissive/refraction BRDFs are essentially the same as specular, but along the view vector
    if (brdfType == TRANSMISSIVE_TYPE)
    {
        const BrdfData data = prepareBRDFData(Nlocal, float3(0.0f, 0.0f, 1.0f) /* unused L vector */, Vlocal, material);
        rayDirectionLocal = sampleSpecularMicrofacet(Vlocal, data.alpha, data.alphaSquared, data.specularF0, u, refractiveIndex, sampleWeight);

        // urgh - please fix
        sampleWeight = material.albedo.xyz * dot(rayDirectionLocal, -Vlocal);

        if ((rayDirectionLocal.x == 0.0f) && (rayDirectionLocal.y == 0.0f) && (rayDirectionLocal.z == 0.0f))
        {
            // total internal reflection
            return false;
        }
        else
        {
            // Transform sampled direction Llocal back to V vector space
            rayDirection = normalize(rotatePoint(invertRotation(qRotationToZ), rayDirectionLocal));

            return true;
        }
    }
    else if (brdfType == DIFFUSE_TYPE)
    {

        // Sample diffuse ray using cosine-weighted hemisphere sampling
        rayDirectionLocal = sampleHemisphere(u);
        const BrdfData data = prepareBRDFData(Nlocal, rayDirectionLocal, Vlocal, material);

        // Function 'diffuseTerm' is predivided by PDF of sampling the cosine weighted hemisphere
        sampleWeight = data.diffuseReflectance * frostbiteDisneyDiffuse(data);

#if COMBINE_BRDFS_WITH_FRESNEL
        // Sample a half-vector of specular BRDF. Note that we're reusing random variable 'u' here, but correctly it should be an new independent random number
        float3 Hspecular = sampleSpecularHalfVector(Vlocal, float2(data.alpha, data.alpha), u);

        // Clamp HdotL to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
        float VdotH = max(0.00001f, min(1.0f, dot(Vlocal, Hspecular)));
        sampleWeight *= (float3(1.0f, 1.0f, 1.0f) - evalFresnel(data.specularF0, shadowedF90(data.specularF0), VdotH));
#endif
        pdf = diffusePdf(data.NdotL);
    }
    else if (brdfType == SPECULAR_TYPE)
    {
        BrdfData data = prepareBRDFData(Nlocal, float3(0.0f, 0.0f, 1.0f) /* unused L vector */, Vlocal, material);
        rayDirectionLocal = sampleSpecularMicrofacet(Vlocal, data.alpha, data.alphaSquared, data.specularF0, u, 0.0f, sampleWeight);

        setBRDFDataLightDirection(data, rayDirectionLocal);
        pdf = sampleGGXVNDFReflectionPdf(data.alpha, data.alphaSquared, data.NdotH, data.NdotV, data.LdotH);
    }

    // Prevent tracing direction with no contribution
    if (luminance(sampleWeight) == 0.0f)
    {
        return false;
    }

    // Transform sampled direction Llocal back to V vector space
    rayDirection = normalize(rotatePoint(invertRotation(qRotationToZ), rayDirectionLocal));

    // Prevent tracing direction "under" the hemisphere (behind the triangle)
    if (dot(geometryNormal, rayDirection) <= 0.0f)
    {
        return false;
    }

    return true;
}

/*

// TODO: Delete this
[shader("miss")]
void ShadowMiss(inout ShadowRayPayload payload : SV_RayPayload)
{
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
*/