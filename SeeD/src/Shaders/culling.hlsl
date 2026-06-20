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

    // clear the front-to-back depth histogram for this frame
    RWStructuredBuffer<uint> sortHistogram = ResourceDescriptorHeap[viewContext.sortHistogramIndex];
    for (uint b = 0; b < SORT_BUCKETS; b++)
        sortHistogram[b] = 0;
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
    
    StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instance.materialIndex];
    
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
        if(material.parameters[4] == 0)
        {
            instanceDesc.Flags = HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_NONE
                               | HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;   
        }
        else
        {
            instanceDesc.Flags = HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_NONE
                               | HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE; 
        }
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
        
        // Reserve space for the whole wave with a single atomic: the first
        // active lane adds the wave total, and each lane derives its slot from
        // the exclusive prefix sum of meshlet counts across active lanes.
        uint waveOffset = WavePrefixSum(lod.meshletCount);
        uint waveTotal = WaveActiveSum(lod.meshletCount);
        uint waveBase = 0;
        if (WaveIsFirstLane())
            InterlockedAdd(counter[1], waveTotal, waveBase);
        waveBase = WaveReadLaneFirst(waveBase);
        uint meshletIndex = waveBase + waveOffset;
        
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

// Quantize a meshlet (given its world-space bounding-sphere center) into a depth bucket:
// 0 = nearest, SORT_BUCKETS-1 = farthest. Linear distance with a logarithmic mapping so near
// geometry (where overdraw matters most) gets the bulk of the bucket resolution, independent of far clip.
uint DepthBucketFromCenter(float3 center, HLSL::Camera camera)
{
    float dist = length(camera.worldPos.xyz - center);
    float nearD = max(camera.nearClip, 1e-4);
    float maxD = max(viewContext.sortMaxDistance, nearD * 2);
    float key = saturate(log2(max(dist, nearD) / nearD) / log2(maxD / nearD));
    return min((uint)(key * SORT_BUCKETS), SORT_BUCKETS - 1);
}

// Ballot of active lanes that share 'bucket'. Also returns the representative (lowest) lane
// of the group and how many lanes are in it, so a single lane can do one aggregated atomic.
uint4 WaveBucketBallot(uint bucket, out uint repLane, out uint laneCount)
{
    uint4 m = WaveMatch(bucket);
    laneCount = countbits(m.x) + countbits(m.y) + countbits(m.z) + countbits(m.w);
    repLane = m.x ? firstbitlow(m.x)
            : m.y ? 32 + firstbitlow(m.y)
            : m.z ? 64 + firstbitlow(m.z)
            : 96 + firstbitlow(m.w);
    return m;
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
        // Each active lane writes one draw call: the first active lane reserves
        // a contiguous block for the whole wave, and each lane takes its slot
        // from the count of active lanes preceding it.
        uint waveOffset = WavePrefixCountBits(true);
        uint waveTotal = WaveActiveCountBits(true);
        uint waveBase = 0;
        if (WaveIsFirstLane())
            InterlockedAdd(meshletCounter[0], waveTotal, waveBase);
        waveBase = WaveReadLaneFirst(waveBase);
        uint index = waveBase + waveOffset;
        HLSL::MeshletDrawCall mdc = (HLSL::MeshletDrawCall) 0;
        mdc.instanceIndex = meshletToCull.instanceIndex;
        mdc.meshletIndex = meshletToCull.meshletIndex;
        mdc.ThreadGroupCountX = 1;
        mdc.ThreadGroupCountY = 1;
        mdc.ThreadGroupCountZ = 1;
        meshletsCulledArgs[index] = mdc;

        if (viewContext.frontToBackSort)
        {
            // We already have this meshlet's world-space center, so derive its depth bucket
            // here for free. Stash it for the scatter pass and accumulate the histogram.
            uint bucket = DepthBucketFromCenter(center, camera);
            RWStructuredBuffer<uint> meshletBuckets = ResourceDescriptorHeap[viewContext.meshletBucketsIndex];
            meshletBuckets[index] = bucket;

            RWStructuredBuffer<uint> sortHistogram = ResourceDescriptorHeap[viewContext.sortHistogramIndex];
            // Wave-aggregate the histogram: lanes sharing a bucket collapse into a single global
            // atomic (one per distinct bucket per wave) instead of one atomic per meshlet.
            uint repLane, bucketLanes;
            WaveBucketBallot(bucket, repLane, bucketLanes);
            if (WaveGetLaneIndex() == repLane)
            {
                uint base;
                InterlockedAdd(sortHistogram[bucket], bucketLanes, base);
            }
        }
    }
}

// ---- Front-to-back draw ordering (bucketed counting sort of the culled meshlet list) ----
// The histogram is built inside CullingMeshlets (which already has each meshlet's center),
// so the only sort-specific passes left are the prefix sum and the scatter.

#pragma compute SortPrefix DrawSortPrefix

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void DrawSortPrefix(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    // exclusive prefix sum, in place: sortHistogram[b] becomes the start offset of bucket b
    RWStructuredBuffer<uint> sortHistogram = ResourceDescriptorHeap[viewContext.sortHistogramIndex];
    uint sum = 0;
    for (uint b = 0; b < SORT_BUCKETS; b++)
    {
        uint count = sortHistogram[b];
        sortHistogram[b] = sum;
        sum += count;
    }
}

#pragma compute SortScatter DrawSortScatter

[RootSignature(SeeDRootSignature)]
[numthreads(DRAW_SORT_THREADS, 1, 1)]
void DrawSortScatter(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    RWStructuredBuffer<uint> meshletCounter = ResourceDescriptorHeap[viewContext.meshletsCounterIndex];
    if (dtid.x >= meshletCounter[0])
        return;

    RWStructuredBuffer<HLSL::MeshletDrawCall> meshletsCulledArgs = ResourceDescriptorHeap[viewContext.meshletsCulledArgsIndex];
    RWStructuredBuffer<HLSL::MeshletDrawCall> meshletsCulledArgsSorted = ResourceDescriptorHeap[viewContext.meshletsCulledArgsSortedIndex];
    RWStructuredBuffer<uint> meshletBuckets = ResourceDescriptorHeap[viewContext.meshletBucketsIndex];
    RWStructuredBuffer<uint> sortHistogram = ResourceDescriptorHeap[viewContext.sortHistogramIndex];

    HLSL::MeshletDrawCall mdc = meshletsCulledArgs[dtid.x];
    uint bucket = meshletBuckets[dtid.x]; // computed once in CullingMeshlets

    // Wave-aggregate the slot reservation: one atomic reserves a contiguous range for all lanes
    // sharing a bucket, and each lane takes a distinct slot within it (intra-bucket order is irrelevant).
    uint repLane, bucketLanes;
    uint4 sameBucket = WaveBucketBallot(bucket, repLane, bucketLanes);
    uint rank = WaveMultiPrefixCountBits(true, sameBucket); // lanes before me sharing this bucket
    uint base = 0;
    if (WaveGetLaneIndex() == repLane)
        InterlockedAdd(sortHistogram[bucket], bucketLanes, base); // running offset within the bucket
    base = WaveReadLaneAt(base, repLane);
    meshletsCulledArgsSorted[base + rank] = mdc;
}
