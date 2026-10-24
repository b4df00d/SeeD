#include "structs.hlsl"

cbuffer CustomSky : register(b3)
{
    HLSL::SkyAtmosphereParameters skyParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"
#include "atmosphere.hlsl"

// The three LUTs of Hillaire 2020, rebuilt every frame (well under a millisecond together) so the
// RenderSettingsVolume blending can change the atmosphere freely. Dispatched in this order, each
// one reads the previous ones.

#pragma compute Transmittance TransmittanceLUT

[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 1)]
void TransmittanceLUT(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= TRANSMITTANCE_LUT_SIZE))
        return;
    HLSL::Atmosphere atmo = viewContext.atmosphere;

    float2 uv = (dtid.xy + 0.5f) / float2(TRANSMITTANCE_LUT_SIZE);
    float viewHeight, viewZenithCos;
    TransmittanceUvToParams(atmo, uv, viewHeight, viewZenithCos);

    float3 pos = float3(0, viewHeight, 0);
    float3 dir = float3(sqrt(1.0f - viewZenithCos * viewZenithCos), viewZenithCos, 0);

    // optical depth to the top of the atmosphere; the parametrization stops at the horizon, so the
    // ray never reaches the ground (below-horizon lookups clamp to it, callers test the planet shadow)
    float tMax = RaySphereNearest(pos, dir, atmo.topRadius);
    const uint sampleCount = 40;
    float dt = max(tMax, 0.0f) / sampleCount;
    float3 opticalDepth = 0.0f;
    for (uint i = 0; i < sampleCount; i++)
        opticalDepth += SampleMedium(atmo, pos + dir * dt * (i + 0.5f)).extinction * dt;

    RWTexture2D<float4> lut = ResourceDescriptorHeap[skyParameters.transmittanceLUTUAV];
    lut[dtid.xy] = float4(exp(-opticalDepth), 1.0f);
}

#pragma compute MultiScattering MultiScatteringLUT

#define MS_SQRT_SAMPLES 8
#define MS_SAMPLES (MS_SQRT_SAMPLES * MS_SQRT_SAMPLES)
groupshared float3 msL[MS_SAMPLES];
groupshared float3 msF[MS_SAMPLES];

// One group per texel, one thread per direction on the sphere (paper section 5.5.2)
[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, MS_SAMPLES)]
void MultiScatteringLUT(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    HLSL::Atmosphere atmo = viewContext.atmosphere;
    uint threadIndex = gtid.z;

    float2 uv = (gid.xy + 0.5f) / MULTISCATTERING_LUT_SIZE;
    uv = float2(FromSubUvToUnit(uv.x, MULTISCATTERING_LUT_SIZE), FromSubUvToUnit(uv.y, MULTISCATTERING_LUT_SIZE));

    float sunZenithCos = uv.x * 2.0f - 1.0f;
    float viewHeight = atmo.bottomRadius + saturate(uv.y + PLANET_RADIUS_OFFSET) * (atmo.topRadius - atmo.bottomRadius - PLANET_RADIUS_OFFSET);
    float3 pos = float3(0, viewHeight, 0);
    float3 sunDir = float3(0, sunZenithCos, sqrt(saturate(1.0f - sunZenithCos * sunZenithCos)));

    // uniform sphere direction
    float a = (0.5f + threadIndex / MS_SQRT_SAMPLES) / MS_SQRT_SAMPLES;
    float b = (0.5f + threadIndex % MS_SQRT_SAMPLES) / MS_SQRT_SAMPLES;
    float theta = 2.0f * ATMO_PI * a;
    float phi = acos(1.0f - 2.0f * b);
    float3 dir = float3(cos(theta) * sin(phi), cos(phi), sin(theta) * sin(phi));

    ScatteringResult r = IntegrateScatteredLuminance(atmo, pos, dir, sunDir, 20, true, false, true, 9000000.0f);

    const float sphereSolidAngle = 4.0f * ATMO_PI;
    msL[threadIndex] = r.L * sphereSolidAngle / MS_SAMPLES;
    msF[threadIndex] = r.multiScatAs1 * sphereSolidAngle / MS_SAMPLES;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = MS_SAMPLES / 2; stride > 0; stride >>= 1)
    {
        if (threadIndex < stride)
        {
            msL[threadIndex] += msL[threadIndex + stride];
            msF[threadIndex] += msF[threadIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadIndex > 0)
        return;

    const float isotropicPhase = 1.0f / sphereSolidAngle;
    float3 inScatteredLuminance = msL[0] * isotropicPhase; // paper eq. 5, L_2nd order
    float3 fms = msF[0] * isotropicPhase;                  // paper eq. 7, transfer of one more order
    float3 psi = inScatteredLuminance / (1.0f - fms);      // paper eq. 9-10, sum of the geometric series

    RWTexture2D<float4> lut = ResourceDescriptorHeap[skyParameters.multiScatteringLUTUAV];
    lut[gid.xy] = float4(psi, 1.0f);
}

#pragma compute SkyView SkyViewLUT

[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 1)]
void SkyViewLUT(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= SKYVIEW_LUT_SIZE))
        return;
    HLSL::Atmosphere atmo = viewContext.atmosphere;

    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];

    float viewHeight = AtmospherePosition(atmo, camera.worldPos.xyz).y;
    float2 uv = (dtid.xy + 0.5f) / float2(SKYVIEW_LUT_SIZE);
    float viewZenithCos, lightViewCos;
    SkyViewUvToParams(atmo, uv, viewHeight, viewZenithCos, lightViewCos);

    // local frame: up = y, the sun's azimuth along +x
    float3 up = float3(0, 1, 0);
    float sunZenithCos = dot(atmo.sunDirection.xyz, up);
    float3 sunDir = float3(sqrt(saturate(1.0f - sunZenithCos * sunZenithCos)), sunZenithCos, 0);
    float viewZenithSin = sqrt(saturate(1.0f - viewZenithCos * viewZenithCos));
    float3 dir = float3(viewZenithSin * lightViewCos, viewZenithCos, viewZenithSin * sqrt(saturate(1.0f - lightViewCos * lightViewCos)));

    float3 pos = float3(0, viewHeight, 0);
    float3 L = 0.0f;
    if (MoveToTopAtmosphere(atmo, pos, dir))
        L = IntegrateScatteredLuminance(atmo, pos, dir, sunDir, 30, false, true, false, 9000000.0f).L;

    RWTexture2D<float4> lut = ResourceDescriptorHeap[skyParameters.skyViewLUTUAV];
    lut[dtid.xy] = float4(L, 1.0f);
}

#pragma compute AerialPerspective AerialPerspectiveLUT

// Camera froxels of the planet atmosphere only (no terrain shadowing): geometry pixels composite it,
// sky pixels must not (the sky-view LUT already integrates the whole ray)
[RootSignature(SeeDRootSignature)]
[numthreads(4, 4, 4)]
void AerialPerspectiveLUT(uint3 dtid : SV_DispatchThreadID)
{
    HLSL::Atmosphere atmo = viewContext.atmosphere;
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];

    float3 dir = FroxelViewDir(dtid.xy, AERIAL_PERSPECTIVE_SIZE, camera);

    float slice = (dtid.z + 0.5f) / AERIAL_PERSPECTIVE_SIZE;
    slice = slice * slice * AERIAL_PERSPECTIVE_SIZE;
    float tMax = AerialPerspectiveSliceToKm(slice);

    float3 pos = AtmospherePosition(atmo, camera.worldPos.xyz);
    float4 result = float4(0, 0, 0, 0);
    if (MoveToTopAtmosphere(atmo, pos, dir))
    {
        // farther slices integrate longer paths: more samples
        uint sampleCount = max(1u, (dtid.z + 1) * 2);
        ScatteringResult r = IntegrateScatteredLuminance(atmo, pos, dir, atmo.sunDirection.xyz, sampleCount, false, true, false, tMax, false);
        float meanTransmittance = dot(exp(-r.opticalDepth), 1.0f / 3.0f);
        result = float4(r.L * atmo.sunIlluminance.xyz, 1.0f - meanTransmittance);
    }

    RWTexture3D<float4> lut = ResourceDescriptorHeap[skyParameters.aerialPerspectiveLUTUAV];
    lut[dtid] = result;
}
