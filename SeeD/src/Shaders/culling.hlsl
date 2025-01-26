//#pragma once

#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Culling


[RootSignature(GlobalRootSignature)]
[numthreads(1, 1, 1)]
void Culling(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
    uint instanceIndex = dtid;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[mesh.meshletOffset];
    
    
    float3 center = mul(instance.worldMatrix, float4(mesh.boundingSphere.xyz, 1)).xyz;
    // assume uniform scaling
    float radius = length(instance.worldMatrix[0].xyz) * mesh.boundingSphere.w;
    
    
}