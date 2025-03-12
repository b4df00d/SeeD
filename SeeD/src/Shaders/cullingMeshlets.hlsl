#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CullingMeshlet

groupshared uint count;
groupshared uint indexRes;
groupshared HLSL::MeshletDrawCall data[HLSL::cullMeshletThreadCount];

[RootSignature(SeeDRootSignature)]
[numthreads(HLSL::cullMeshletThreadCount, 1, 1)]
void CullingMeshlet(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    
    if (gtid.x == 0)
    {
        count = 0;
        indexRes = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    
    StructuredBuffer<HLSL:: Mesh > meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    uint localMeshletIndex = dtid.x;
    if (localMeshletIndex >= mesh.meshletCount)
        return;
    uint meshletIndex = mesh.meshletOffset + localMeshletIndex;
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletIndex];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cullingContext.cameraIndex];
    
    RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[cullingContext.meshletsCounterIndex];
    RWStructuredBuffer<HLSL::MeshletDrawCall> meshletsInView = ResourceDescriptorHeap[cullingContext.culledMeshletsIndex];
    
    float3 center = mul(instance.worldMatrix, float4(meshlet.boundingSphere.xyz, 1)).xyz;
    float radius = abs(max(max(length(instance.worldMatrix[0].xyz), length(instance.worldMatrix[1].xyz)), length(instance.worldMatrix[2].xyz)) * meshlet.boundingSphere.w); // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if (!culled)
        culled = OcclusionCulling(camera, boundingSphere);
    
    if (!culled)
    {
        uint index = 0;
        InterlockedAdd(count, 1, index);
        HLSL::MeshletDrawCall mdc = (HLSL::MeshletDrawCall) 0;
        mdc.instanceIndex = instanceIndexIndirect;
        mdc.meshletIndex = meshletIndex;
        mdc.ThreadGroupCountX = 1;
        mdc.ThreadGroupCountY = 1;
        mdc.ThreadGroupCountZ = 1;
        data[index] = mdc;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    if (count == 0)
        return;
 
    if (gtid.x == 0)
    {
        InterlockedAdd(counter[0], count, indexRes);
    }
    
    GroupMemoryBarrierWithGroupSync();
    if (gtid.x < count)
    {
        meshletsInView[indexRes + gtid.x] = data[gtid.x];
    }
}