#include "structs.hlsl"

cbuffer CustomPPHR : register(b3)
{
    HLSL::PostProcessHalfResParameters pphrParameters;
};
#define CUSTOM_ROOT_BUFFER_1

cbuffer CustomAS : register(b4)
{
    HLSL::AtmosphericScatteringParameters asParameters;
};
#define CUSTOM_ROOT_BUFFER_2

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute PostProcessHalfRes PostProcessHalfRes

// 0: fog froxels + aerial perspective, 1: fog froxels only, 2: aerial perspective (32^3 volume) only,
// 3: false color (R lookup depth, G fog opacity, B distance), 4: fog radiance / sun illuminance (x10, red above 1)
#define ATMO_DEBUG 0

// Froxel slices (log spaced, so a constant fraction of the depth, ~6-7% each) the fog lookup is pulled
// toward the camera. A froxel is ~16 pixels wide and is read bilinearly (plus the 3x3 tent of the
// accumulation): a wall pixel next to a window also reads columns whose rays went through it, and at the
// wall's depth those rays are already outside, in the sunlit exterior fog. A little in front of the wall
// they are still inside the room. Costs that much fog in front of every surface; 0: unbiased lookup
#define FOG_COMPOSITE_DEPTH_BIAS 0.0f

// The slice holding the surface: its segment can hold medium from behind the surface (a jittered
// sample whose ray missed the surface, outside the building), and trilinear filtering reads it.
// 1: never read that slice: the integral stops at the last slice boundary in front of the surface and
// the remaining fraction of a slice is extrapolated from the segment in front (slices are log spaced,
// consecutive segments differ by ~7% in length). 0: plain trilinear
#define FOG_COMPOSITE_REJECT_BEHIND 0

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void PostProcessHalfRes(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if (dtid.x >= viewContext.renderResolution.x || dtid.y >= viewContext.renderResolution.y)
        return;
    
    uint2 renderPixel = dtid.xy;
    GBufferCameraData cd = GetGBufferCameraData(renderPixel.xy);
    
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[pphrParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[pphrParameters.atmosphericScatteringIndex];
    Texture3D<float4> atmosphericScattering = ResourceDescriptorHeap[currentFroxel.srvIndex];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    float3 froUV = WorldToFroxelUVW(cd.worldPos, currentFroxel.resolution.xyz, asParameters.specialNear, camera);
    
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[pphrParameters.lightedIndex];
    float4 input = lighted[renderPixel.xy];
    RWTexture2D<float4> transparencyLayer = ResourceDescriptorHeap[pphrParameters.transparencyLayerIndex];

    bool sky = cd.reverseZ <= 0;

    // Local fog froxels. xyz = in-scattered radiance S integrated camera -> pixel (premultiplied by
    // nature), w = optical depth. Sky pixels see the whole froxel range
    if (sky)
        froUV.z = 1;
    else // coordinate 0 (texel center 0.5 / size) is the camera itself
        froUV.z = max(froUV.z - FOG_COMPOSITE_DEPTH_BIAS / currentFroxel.resolution.z, 0.5f / currentFroxel.resolution.z);
#if FOG_COMPOSITE_REJECT_BEHIND
    float4 fogData;
    if (sky)
        fogData = atmosphericScattering.Sample(samplerLinearClamp, froUV);
    else
    {
        // texel z holds the integral from the camera to coordinate z (slice 0: the camera, 0)
        float coordinate = froUV.z * currentFroxel.resolution.z - 0.5f;
        float front = floor(coordinate);
        float fraction = coordinate - front;
        float invSize = 1.0f / currentFroxel.resolution.z;
        float4 atFront = atmosphericScattering.Sample(samplerLinearClamp, float3(froUV.xy, (front + 0.5f) * invSize));
        float4 beforeFront = front >= 1.0f ? atmosphericScattering.Sample(samplerLinearClamp, float3(froUV.xy, (front - 0.5f) * invSize)) : 0.0f;
        // both are integrals from the camera: the difference is the last whole segment, never negative
        fogData = atFront + fraction * max(atFront - beforeFront, 0.0f);
    }
#else
    float4 fogData = atmosphericScattering.Sample(samplerLinearClamp, froUV);
#endif
    float fogTransmittance = exp(-fogData.w);

    // Planet atmosphere between the camera and the surface. Not for sky pixels: the sky-view LUT
    // already integrates the whole ray
    float4 ap = float4(0, 0, 0, 1);
    if (!sky)
        ap = SampleAerialPerspective(viewContext.atmosphere, (renderPixel + 0.5f) * viewContext.renderResolution.zw, cd.viewDist);

#if ATMO_DEBUG == 1
    ap = float4(0, 0, 0, 1);                        // fog froxels only
#elif ATMO_DEBUG == 2
    fogData = 0; fogTransmittance = 1;              // aerial perspective (32^3) only
#endif

    // The fog is treated as entirely in front of the atmosphere: L = (L_surface * T_ap + S_ap) * T_fog + S_fog
    float transmittance = ap.w * fogTransmittance;
    float3 inScatter = ap.xyz * fogTransmittance + fogData.xyz;

#if ATMO_DEBUG == 3
    // False color, replaces the image. R: froxel lookup depth (froUV.z, 0 camera -> 1 far clip),
    // G: fog opacity 1 - T_fog, B: distance / 100 m. On ground receding to the horizon R and G
    // should both grow smoothly with distance
    float3 debugColor = float3(froUV.z, 1.0f - fogTransmittance, saturate(cd.viewDist / 100.0f));
    transparencyLayer[renderPixel.xy] = float4(debugColor, 1.0f);
    lighted[renderPixel.xy] = float4(debugColor, 1.0f);
    return;
#elif ATMO_DEBUG == 4
    // Fog radiance L_fog = S / (1 - T): what the fog converges to once opaque, divided by the sun
    // illuminance. Expected: ~0.01-0.05 looking away from the sun (side/back scattering + sky),
    // up to ~0.8 looking toward it (g = 0.6 forward peak). Gray = fine, red = above 1 (wrong)
    float fogOpacity = 1.0f - fogTransmittance;
    float sunE = max(dot(viewContext.atmosphere.sunIlluminance.xyz, 1.0f / 3.0f), 1e-6f);
    float fogRadiance = fogOpacity > 1e-3f ? dot(fogData.xyz, 1.0f / 3.0f) / fogOpacity / sunE : 0.0f;
    float3 debugColor = fogRadiance > 1.0f ? float3(1, 0, 0) : fogRadiance.xxx * 10.0f; // x10: 0.1 -> white
    transparencyLayer[renderPixel.xy] = float4(debugColor, 1.0f);
    lighted[renderPixel.xy] = float4(debugColor, 1.0f);
    return;
#endif

    // L = L_surface * T + S
    if (viewContext.upscaling == HLSL::Upscaling::dlssd)
    {
        // The input color stays the clean opaque image (it must match the albedo/normal guides for
        // the denoiser); DLSS-RR composites the premultiplied layer over its denoised output and
        // applies (1 - alpha) to the input itself -- verified: a constant (0, 0, 0, 1) layer over an
        // untouched input renders fully black. Do not pre-multiply the input by transmittance.
        transparencyLayer[renderPixel.xy] = float4(inScatter, 1.0 - transmittance);
    }
    else
    {
        input.xyz = input.xyz * transmittance + inScatter;
        lighted[renderPixel.xy] = input;
    }
}
