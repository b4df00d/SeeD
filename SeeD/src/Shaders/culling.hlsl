#include "structs.hlsl"

cbuffer CustomRT : register(b3)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Reset CullingReset

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void CullingReset(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint instanceIndex = dtid.x;
    
    RWStructuredBuffer<uint> isntancesCounters = ResourceDescriptorHeap[viewContext.instancesCounterIndex];
    isntancesCounters[0] = 0;
    isntancesCounters[1] = 0;
    
    RWStructuredBuffer<uint> meshletsCounters = ResourceDescriptorHeap[viewContext.meshletsCounterIndex];
    meshletsCounters[0] = 0;
    
    RWStructuredBuffer<uint> instanceRaytracingCounter = ResourceDescriptorHeap[rtParameters.instancesRaytracingCountHeapIndex];
    instanceRaytracingCounter[0] = 0;
}

#pragma compute Instances CullingInstances

[RootSignature(SeeDRootSignature)]
[numthreads(INSTANCE_CULLING_THREADS, 1, 1)]
void CullingInstances(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
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
  
    {
        // RT instances
        RWStructuredBuffer<uint> instanceRaytracingCounter = ResourceDescriptorHeap[rtParameters.instancesRaytracingCountHeapIndex];
        RWStructuredBuffer<HLSL::D3D12_RAYTRACING_INSTANCE_DESC> instanceRaytracing = ResourceDescriptorHeap[rtParameters.instancesRaytracingHeapIndex];
    
        HLSL::D3D12_RAYTRACING_INSTANCE_DESC instanceDesc;
        instanceDesc.InstanceID = instanceIndex;
        instanceDesc.InstanceContributionToHitGroupIndex = 0;
        instanceDesc.Flags = HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_NONE 
                           | HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE 
                           | HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
        instanceDesc.Transform[0][0] = worldMatrix[0][0];
        instanceDesc.Transform[0][1] = worldMatrix[0][1];
        instanceDesc.Transform[0][2] = worldMatrix[0][2];
        instanceDesc.Transform[0][3] = worldMatrix[0][3];
        instanceDesc.Transform[1][0] = worldMatrix[1][0];
        instanceDesc.Transform[1][1] = worldMatrix[1][1];
        instanceDesc.Transform[1][2] = worldMatrix[1][2];
        instanceDesc.Transform[1][3] = worldMatrix[1][3];
        instanceDesc.Transform[2][0] = worldMatrix[2][0];
        instanceDesc.Transform[2][1] = worldMatrix[2][1];
        instanceDesc.Transform[2][2] = worldMatrix[2][2];
        instanceDesc.Transform[2][3] = worldMatrix[2][3];
        instanceDesc.InstanceMask = 0xFF;
        instanceDesc.AccelerationStructure = instance.rayTracingBLAS;
        
        uint instanceRTIndex = 0;
        InterlockedAdd(instanceRaytracingCounter[0], 1, instanceRTIndex);
        instanceRaytracing[instanceRTIndex] = instanceDesc;
    }
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if(!culled) culled = OcclusionCulling(camera, boundingSphere);
    
    if (!culled)
    {
        // we can chose the mesh LOD here also
        uint lodIndex = max(min(log2(dist * 0.05 / radius  + 1) * 3, 3), 0);
        
        HLSL::Mesh::LOD lod = mesh.LODs[lodIndex];
        
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
    }
}

#pragma compute Count CountMeshletsDispatch

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void CountMeshletsDispatch(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint instanceIndex = dtid.x;
    
    RWStructuredBuffer<uint> instancesCounters = ResourceDescriptorHeap[viewContext.instancesCounterIndex];
    RWStructuredBuffer<HLSL::InstanceCullingDispatch> instancesCulledArgs = ResourceDescriptorHeap[viewContext.instancesCulledArgsIndex];
    
    instancesCounters[0] = 1;
    HLSL::InstanceCullingDispatch idc = (HLSL::InstanceCullingDispatch) 0;
    idc.instanceIndex = 0;
    idc.meshletIndex = 0;
    idc.ThreadGroupCountX = ceil(instancesCounters[1] / (GROUPED_CULLING_THREADS * 1.0f));
    idc.ThreadGroupCountY = 1;
    idc.ThreadGroupCountZ = 1;
    instancesCulledArgs[0] = idc;
}

#pragma compute Meshlets CullingMeshlets

[RootSignature(SeeDRootSignature)]
[numthreads(GROUPED_CULLING_THREADS, 1, 1)]
void CullingMeshlets(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
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
