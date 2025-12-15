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

#pragma compute AtmosphericScatteringReprojection

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
    
    float3 currentWorldPos = FroxelToWorld(dtid.xyz, currentFroxel.resolution.xyz, 0.1, camera);
    float3 historyFroxelPos = WorldTohistoryFroxelUVW(currentWorldPos, historyFroxel.resolution.xyz, 0.1, camera);
    
    Texture3D<float4> historyFroxelData = ResourceDescriptorHeap[historyFroxel.index];
    float4 previousData = historyFroxelData.Sample(samplerLinearClamp, historyFroxelPos);
    
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    if (viewContext.frameNumber > 10)
        froxelData[dtid.xyz] = lerp(previousData, froxelData[dtid.xyz], 0.5);
}
