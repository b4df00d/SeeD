//#pragma once

#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CullingMeshlet

[RootSignature(GlobalRootSignature)]
[numthreads(1, 1, 1)]
void CullingMeshlet(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
    uint localMeshletIndex = dtid;
    uint meshletIndex = meshletIndexIndirect + localMeshletIndex;
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cullingContext.cameraIndex];
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletIndex];
    
    RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[cullingContext.meshletsCounterIndex];
    RWStructuredBuffer<HLSL::MeshletDrawCall> meshletsInView = ResourceDescriptorHeap[cullingContext.culledMeshletsIndex];
    
    float3 center = mul(instance.worldMatrix, float4(meshlet.boundingSphere.xyz, 1)).xyz;
    float radius = length(instance.worldMatrix[0].xyz) * meshlet.boundingSphere.w; // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = FrustumCulling(camera.planes, boundingSphere);
    
    if (!culled)
    {
        uint index = 0;
        InterlockedAdd(counter[0], 1, index);
        HLSL::MeshletDrawCall mdc = (HLSL::MeshletDrawCall) 0;
        mdc.instanceIndex = instanceIndexIndirect;
        mdc.meshletIndex = meshletIndex;
        mdc.ThreadGroupCountX = 1;
        mdc.ThreadGroupCountY = 1;
        mdc.ThreadGroupCountZ = 1;
        meshletsInView[index] = mdc;
    }
}