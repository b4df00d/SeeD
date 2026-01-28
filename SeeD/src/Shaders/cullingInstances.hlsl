#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CullingInstance

[RootSignature(SeeDRootSignature)]
[numthreads(INSTANCE_CULLING_THREADS, 1, 1)]
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
    RWStructuredBuffer<HLSL::InstanceCullingDispatch> instancesCulledArgs = ResourceDescriptorHeap[viewContext.instancesCulledArgsIndex];
    
    float4x4 worldMatrix = instance.unpack(instance.current);
    float3 center = mul(worldMatrix, float4(mesh.boundingSphere.xyz, 1)).xyz;
    float radius = abs(instance.GetScale() * mesh.boundingSphere.w); // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    float dist = length(camera.worldPos.xyz - center.xyz);
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if(!culled) culled = OcclusionCulling(camera, boundingSphere);
    
    if (!culled)
    {
        // we can chose the mesh LOD here also
        uint lodIndex = max(min(log2(dist * 0.05 / radius  + 1) * 3, 3), 0);
        
        HLSL::Mesh::LOD lod = mesh.LODs[lodIndex];
        
        #if GROUPED_CULLING
        
        uint meshletIndex = 0;
        InterlockedAdd(counter[1], lod.meshletCount, meshletIndex);
        
        RWStructuredBuffer<HLSL::GroupedCullingDispatch> meshletsToCull = ResourceDescriptorHeap[viewContext.meshletsToCullIndex];
        HLSL::GroupedCullingDispatch mdc = (HLSL::GroupedCullingDispatch) 0;
        mdc.instanceIndex = instanceIndex;
        for (uint i = 0; i < lod.meshletCount; i++)
        {
            mdc.meshletIndex = lod.meshletOffset+i;
            meshletsToCull[meshletIndex+i] = mdc;
        }
        
        #else
        
        uint index = 0;
        InterlockedAdd(counter[0], 1, index);
        HLSL::InstanceCullingDispatch mdc = (HLSL::InstanceCullingDispatch) 0;
        mdc.instanceIndex = instanceIndex;
        mdc.meshletIndex = mesh.meshletOffset;
        mdc.ThreadGroupCountX = ceil(mesh.meshletCount / (HLSL::cullMeshletThreadCount * 1.0f));
        mdc.ThreadGroupCountY = 1;
        mdc.ThreadGroupCountZ = 1;
        instancesCulledArgs[index] = mdc;
        
        #endif
    }
}
