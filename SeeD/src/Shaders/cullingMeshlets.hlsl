#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CullingMeshlet

[RootSignature(SeeDRootSignature)]
[numthreads(HLSL::cullMeshletThreadCount, 1, 1)]
void CullingMeshlet(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    
    StructuredBuffer<HLSL:: Mesh > meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    uint localMeshletIndex = dtid;
    if (localMeshletIndex >= mesh.meshletCount)
        return;
    uint meshletIndex = mesh.meshletOffset + localMeshletIndex;
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletIndex];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cullingContext.cameraIndex];
    
    float3 center = mul(instance.worldMatrix, float4(meshlet.boundingSphere.xyz, 1)).xyz;
    float radius = length(instance.worldMatrix[0].xyz) * meshlet.boundingSphere.w; // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if (!culled)
        culled = OcclusionCulling(camera, boundingSphere);
    
    if (!culled)
    {
        RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[cullingContext.meshletsCounterIndex];
        RWStructuredBuffer<HLSL::MeshletDrawCall> meshletsInView = ResourceDescriptorHeap[cullingContext.culledMeshletsIndex];
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