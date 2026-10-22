#include "structs.hlsl"

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
#define FOG_DEBUG_LIGHTS                0 // 0: analytic lights + sky, 1: analytic lights only (RIS + shadow ray), 2: sky only
#define BOUNCES_MIN                     1
#define RIS_CANDIDATES_LIGHTS           8 // Number of candidates used for resampling of analytical lights
#define SHADOW_RAY_IN_RIS               0 // Enable this to cast shadow rays for each candidate during resampling. This is expensive but increases quality
#define ENABLE_SPECULAR_LOBE            1 // Enable to use the specular lobe for splitting between diffuse and specular BRDFs

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

// Fog medium at a world position: the global fog plus every local source. The single place to add
// density sources (particles, a painted fog map, ...)
void FogMedium(float3 worldPos, out float3 sigmaS, out float3 sigmaT)
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

    // ---- medium ----
    float3 sigmaS, sigmaT;
    FogMedium(worldPos, sigmaS, sigmaT);
    float g = asParameters.fogScattering.w;

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
    // Sky light: one occlusion-traced direction importance sampled from the phase function, so
    // phase / pdf = 1 and the estimate is just the visible sky radiance
    {
        float3 dir = SampleHenyeyGreenstein(g, viewDir, float2(RNG(rngState), RNG(rngState)));
        float3 visibility = CastShadowRay(AccelerationStructure, worldPos, 0, dir, TRACING_DISTANCE);
        if (any(visibility > 0.0f))
            inScattered += Sky(dir) * visibility;
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
