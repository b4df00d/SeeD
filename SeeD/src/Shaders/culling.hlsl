//#pragma once

#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Culling


[RootSignature(GlobalRootSignature)]
[numthreads(8, 1, 1)]
void Culling(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
    uint instanceIndex = dtid;
    
    if (instanceIndex >= commonResourcesIndices.instanceCount)
        return;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    RWStructuredBuffer<uint> counters = ResourceDescriptorHeap[cullingContext.culledInstanceCounterIndex];
    RWStructuredBuffer<HLSL::MeshletDrawCall> meshletsInView = ResourceDescriptorHeap[cullingContext.culledInstanceIndex];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cullingContext.cameraIndex];
    
    float3 center = mul(instance.worldMatrix, float4(mesh.boundingSphere.xyz, 1)).xyz;
    float radius = length(instance.worldMatrix[0].xyz) * mesh.boundingSphere.w; // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = FrustumCulling(camera.planes, boundingSphere);
    
    if (!culled)
    {
        for (uint i = 0; i < mesh.meshletCount; i++)
        {
            StructuredBuffer<HLSL:: Meshlet > meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
            HLSL::Meshlet meshlet = meshlets[mesh.meshletOffset + i];
        
            center = mul(instance.worldMatrix, float4(meshlet.boundingSphere.xyz, 1)).xyz;
            radius = length(instance.worldMatrix[0].xyz) * meshlet.boundingSphere.w; // assume uniform scaling
            boundingSphere = float4(center, radius);
    
            culled = FrustumCulling(camera.planes, boundingSphere);
    
            if (!culled)
            {
                uint index = 0;
                InterlockedAdd(counters[0], 1, index);
                HLSL::MeshletDrawCall mdc = (HLSL::MeshletDrawCall)0;
                mdc.meshletIndex = mesh.meshletOffset + i;
                mdc.instanceIndex = instanceIndex;
                mdc.ThreadGroupCountX = 1;
                mdc.ThreadGroupCountY = 1;
                mdc.ThreadGroupCountZ = 1;
                meshletsInView[index] = mdc;
            }
        }
    }

}