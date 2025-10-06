#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CullingInstance

[RootSignature(SeeDRootSignature)]
[numthreads(128, 1, 1)]
void CullingInstance(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint instanceIndex = dtid.x;
    if (instanceIndex >= commonResourcesIndices.instanceCount)
        return;
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[viewContext.instancesCounterIndex];
    RWStructuredBuffer<HLSL::InstanceCullingDispatch> instancesInView = ResourceDescriptorHeap[viewContext.culledInstanceIndex];
    
    float4x4 worldMatrix = instance.unpack(instance.current);
    float3 center = mul(worldMatrix, float4(mesh.boundingSphere.xyz, 1)).xyz;
    float radius = abs(instance.GetScale() * mesh.boundingSphere.w); // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = false;//FrustumCulling(camera, boundingSphere);
    if(!culled) culled = OcclusionCulling(camera, boundingSphere);
    
    if (!culled)
    {
        // we can chose the mesh LOD here also
        
        uint index = 0;
        InterlockedAdd(counter[0], 1, index);
        HLSL::InstanceCullingDispatch mdc = (HLSL::InstanceCullingDispatch) 0;
        mdc.instanceIndex = instanceIndex;
        mdc.meshletIndex = mesh.meshletOffset;
        mdc.ThreadGroupCountX = ceil(mesh.meshletCount / (HLSL::cullMeshletThreadCount * 1.0f));
        mdc.ThreadGroupCountY = 1;
        mdc.ThreadGroupCountZ = 1;
        instancesInView[index] = mdc;
    }
}
