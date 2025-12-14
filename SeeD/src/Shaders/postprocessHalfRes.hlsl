#include "structs.hlsl"

cbuffer CustomPPHR : register(b3)
{
    HLSL::PostProcessHalfResParameters pphrParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute PostProcessHalfRes


[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void PostProcessHalfRes(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y)
        return;
    
    uint2 renderPixel = dtid.xy;
    GBufferCameraData cd = GetGBufferCameraData(renderPixel.xy);
    
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[pphrParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[pphrParameters.atmosphericScatteringIndex];
    Texture3D<float4> atmosphericScattering = ResourceDescriptorHeap[currentFroxel.index];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    float3 froUV = WorldToFroxel(cd.worldPos, currentFroxel.resolution.xyz, 0.1, camera);
    
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[pphrParameters.lightedIndex];
    float4 input = lighted[renderPixel.xy];
    float4 atmoData = atmosphericScattering.Sample(samplerLinearClamp, froUV);
    input.xyz = lerp(input.xyz, atmoData.xyz, atmoData.w);
    lighted[renderPixel.xy] = input;
}
