#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CullingInstance

groupshared uint count;
groupshared uint indexRes;
groupshared HLSL::InstanceCullingDispatch data[HLSL::cullInstanceThreadCount];

[RootSignature(SeeDRootSignature)]
[numthreads(HLSL::cullInstanceThreadCount, 1, 1)]
void CullingInstance(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if (gtid.x == 0)
    {
        count = 0;
        indexRes = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    
    uint instanceIndex = dtid.x;
    if (instanceIndex >= commonResourcesIndices.instanceCount)
        return;
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cullingContext.cameraIndex];
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[cullingContext.instancesCounterIndex];
    RWStructuredBuffer<HLSL::InstanceCullingDispatch> instancesInView = ResourceDescriptorHeap[cullingContext.culledInstanceIndex];
    
    
    float3 center = mul(instance.worldMatrix, float4(mesh.boundingSphere.xyz, 1)).xyz;
    float radius = abs(max(max(length(instance.worldMatrix[0].xyz), length(instance.worldMatrix[1].xyz)), length(instance.worldMatrix[2].xyz)) * mesh.boundingSphere.w); // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if(!culled)
        culled = OcclusionCulling(camera, boundingSphere);
    
    if (!culled)
    {
        uint index = 0;
        InterlockedAdd(count, 1, index);
        
        HLSL::InstanceCullingDispatch mdc = (HLSL::InstanceCullingDispatch) 0;
        mdc.instanceIndex = instanceIndex;
        mdc.meshletIndex = mesh.meshletOffset;
        mdc.ThreadGroupCountX = ceil(mesh.meshletCount / (HLSL::cullMeshletThreadCount * 1.0f));
        mdc.ThreadGroupCountY = 1;
        mdc.ThreadGroupCountZ = 1;
        data[index] = mdc;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    if (count == 0)
        return;
 
    if(gtid.x == 0)
    {
        InterlockedAdd(counter[0], count, indexRes);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid.x < count)
    {
        instancesInView[indexRes + gtid.x] = data[gtid.x];
    }
}
