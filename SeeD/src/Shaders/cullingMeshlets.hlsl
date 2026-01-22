#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CullingMeshlet


#if GROUPED_CULLING
[RootSignature(SeeDRootSignature)]
[numthreads(GROUPED_CULLING_THREADS, 1, 1)]
void CullingMeshlet(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[viewContext.instancesCounterIndex];
    
    uint localMeshletIndex = dtid.x;
    if (localMeshletIndex >= counter[1])
        return;
            
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    RWStructuredBuffer<HLSL::GroupedCullingDispatch> meshletsToCull = ResourceDescriptorHeap[viewContext.meshletsToCullIndex];
    HLSL::GroupedCullingDispatch meshletToCull = meshletsToCull[localMeshletIndex];
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[meshletToCull.instanceIndex];
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletToCull.meshletIndex];
    
    float4x4 worldMatrix = instance.unpack(instance.current);
    float3 center = mul(worldMatrix, float4(meshlet.boundingSphere.xyz, 1)).xyz;
    float radius = abs(max(max(length(worldMatrix[0].xyz), length(worldMatrix[1].xyz)), length(worldMatrix[2].xyz)) * meshlet.boundingSphere.w); // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if (!culled)  culled = OcclusionCulling(camera, boundingSphere);
    
    if (!culled)
    {
        RWStructuredBuffer<uint> meshletCounter = ResourceDescriptorHeap[viewContext.meshletsCounterIndex];
        RWStructuredBuffer<HLSL::MeshletDrawCall> meshletsCulledArgs = ResourceDescriptorHeap[viewContext.meshletsCulledArgsIndex];
        uint index = 0;
        InterlockedAdd(meshletCounter[0], 1, index);
        HLSL::MeshletDrawCall mdc = (HLSL::MeshletDrawCall) 0;
        mdc.instanceIndex = meshletToCull.instanceIndex;
        mdc.meshletIndex = meshletToCull.meshletIndex;
        mdc.ThreadGroupCountX = 1;
        mdc.ThreadGroupCountY = 1;
        mdc.ThreadGroupCountZ = 1;
        meshletsCulledArgs[index] = mdc;
    }
}
#else
groupshared HLSL::Instance instance;
groupshared HLSL::Mesh mesh;
groupshared HLSL::Camera camera;
[RootSignature(SeeDRootSignature)]
[numthreads(HLSL::cullMeshletThreadCount, 1, 1)]
void CullingMeshlet(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if(gtid.x == 0)
    {
        StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
        instance = instances[instanceIndexIndirect];
        
        StructuredBuffer<HLSL:: Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
        mesh = meshes[instance.meshIndex];
        
        StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
        camera = cameras[viewContext.cameraIndex];
    }
    GroupMemoryBarrierWithGroupSync();
    
    uint localMeshletIndex = dtid.x;
    if (localMeshletIndex >= mesh.meshletCount)
        return;
    
    uint meshletIndex = mesh.meshletOffset + localMeshletIndex;
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletIndex];
    
    
    float4x4 worldMatrix = instance.unpack(instance.current);
    float3 center = mul(worldMatrix, float4(meshlet.boundingSphere.xyz, 1)).xyz;
    float radius = abs(max(max(length(worldMatrix[0].xyz), length(worldMatrix[1].xyz)), length(worldMatrix[2].xyz)) * meshlet.boundingSphere.w); // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if (!culled)  culled = OcclusionCulling(camera, boundingSphere);
    
    if (!culled)
    {
        RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[viewContext.meshletsCounterIndex];
        RWStructuredBuffer<HLSL::MeshletDrawCall> meshletsCulledArgs = ResourceDescriptorHeap[viewContext.meshletsCulledArgsIndex];
        uint index = 0;
        InterlockedAdd(counter[0], 1, index);
        HLSL::MeshletDrawCall mdc = (HLSL::MeshletDrawCall) 0;
        mdc.instanceIndex = instanceIndexIndirect;
        mdc.meshletIndex = meshletIndex;
        mdc.ThreadGroupCountX = 1;
        mdc.ThreadGroupCountY = 1;
        mdc.ThreadGroupCountZ = 1;
        meshletsCulledArgs[index] = mdc;
    }
}
#endif