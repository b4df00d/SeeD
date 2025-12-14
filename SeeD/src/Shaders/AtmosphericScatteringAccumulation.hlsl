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

#pragma compute AtmosphericScatteringAccumulation

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void AtmosphericScatteringAccumulation(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint seed = initRand(dtid.xy * dtid.z);
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[asParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[asParameters.currentFroxelIndex];
    HLSL::Froxels historyFroxel = froxels[asParameters.historyFroxelIndex];
    
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    RWTexture3D<float4> historyFroxelData = ResourceDescriptorHeap[historyFroxel.index];
    
    float4 accumulatedRadiance = float4(0, 0, 0, 0);
    for (uint z = 0; z < currentFroxel.resolution.z; z++)
    {
        uint3 fro = uint3(dtid.xy, z);
        float4 froData = froxelData[fro];
        historyFroxelData[fro] = froData;
        accumulatedRadiance.xyz += exp(-accumulatedRadiance.w) * (froData.xyz * froData.w);
        accumulatedRadiance.w += froData.w;
        froxelData[fro] = accumulatedRadiance;
    }
}
