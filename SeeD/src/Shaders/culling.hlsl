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
    for (uint b0 = 0; b0 < viewContext.shaderBucketCount; b0++)
        meshletsCounters[b0] = 0; // one draw count per shader bucket (bucket 0 = sort-eligible default opaque shader)

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
    // CPU-computed world-space override (currently: terrain quadtree nodes, see World.h
    // Systems::TerrainStreaming::CreateNodeInstance) takes the real per-node height range into
    // account, which the shared mesh AABB can't (terrain's grid mesh is flat, displaced only in the
    // mesh shader). w <= 0 means no override -- fall back to the mesh-derived sphere as before.
    float4 meshBS = mesh.GetBoundingSphere();
    float4 meshDerivedSphere = float4(mul(worldMatrix, float4(meshBS.xyz, 1)).xyz, abs(instance.GetScale() * meshBS.w)); // assume uniform scaling
    float4 boundingSphere = (instance.boundingSphereOverride.w > 0) ? instance.boundingSphereOverride : meshDerivedSphere;
    float dist = length(camera.worldPos.xyz - boundingSphere.xyz);
    // mesh LOD from distance; also picks which of the two BLAS the TLAS instance references
    float rawLod = log2(dist * viewContext.lodDistanceMultiplier / boundingSphere.w + 1) * 3;
    uint lodIndex = max(min(rawLod, 3), 0);
    
    
    {
        // RT instances
        RWStructuredBuffer<uint> instanceRaytracingCounter = ResourceDescriptorHeap[rtParameters.instancesRaytracingCountHeapIndex];
        RWStructuredBuffer<HLSL::D3D12_RAYTRACING_INSTANCE_DESC> instanceRaytracing = ResourceDescriptorHeap[rtParameters.instancesRaytracingHeapIndex];

        bool lowLod = lodIndex >= 1;

        HLSL::D3D12_RAYTRACING_INSTANCE_DESC instanceDesc;
        instanceDesc.InstanceID = instanceIndex | (lowLod ? HLSL::RTInstanceLowLodBit : 0);
        instanceDesc.InstanceContributionToHitGroupIndex = 0;
        if(material.parameters[6] == 0)
        {
            instanceDesc.Flags = HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_NONE
                               | HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;   
        }
        else
        {
            instanceDesc.Flags = HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_NONE
                               | HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE; 
        }
        
        // material swapped since the mesh's OMM bake: opt this instance out of the (now wrong)
        // opacity states; its BLAS was built with ALLOW_DISABLE_OMMS so the flag is legal
        if (instance.rtFlags & HLSL::RTInstanceFlagDisableOMMs)
            instanceDesc.Flags |= HLSL::D3D12_RAYTRACING_INSTANCE_FLAGS::D3D12_RAYTRACING_INSTANCE_FLAG_DISABLE_OMMS;

        instanceDesc.InstanceMask = 0xFF;
        
    
        bool culled = rawLod > viewContext.distanceCullingValueRT;
        if(culled)
            instanceDesc.AccelerationStructure = 0;
        else
        {
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
            instanceDesc.AccelerationStructure = lowLod ? instance.rayTracingBLASLow : instance.rayTracingBLAS;
        }
        
        // Reserve one slot per active lane with a single wave-aggregated atomic:
        // the first active lane adds the wave total, and each lane derives its
        // slot from the count of active lanes preceding it.
        uint waveOffset = WavePrefixCountBits(true);
        uint waveTotal = WaveActiveCountBits(true);
        uint waveBase = 0;
        if (WaveIsFirstLane())
            InterlockedAdd(instanceRaytracingCounter[0], waveTotal, waveBase);
        waveBase = WaveReadLaneFirst(waveBase);
        uint instanceRTIndex = waveBase + waveOffset;
        instanceRaytracing[instanceRTIndex] = instanceDesc;
    }
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if(!culled) culled = OcclusionCulling(camera, boundingSphere);
    culled |= rawLod > viewContext.distanceCullingValue;
    
    lodIndex = min(lodIndex, mesh.lodCount - 1);

    // One record per instance instead of expanding its meshlets here: this pass has only one thread
    // per instance, so writing N meshlets per thread serialized whole waves behind their largest
    // instance. InstanceMeshletPrefix scans the counts and CullingMeshlets (one thread per meshlet)
    // finds its owning instance by binary search.
    RWStructuredBuffer<HLSL::InstanceMeshletRange> instanceMeshletRanges = ResourceDescriptorHeap[viewContext.instanceMeshletRangesIndex];
    HLSL::InstanceMeshletRange range;
    range.meshletPrefix = 0; // meshlet count until scanned
    range.firstMeshlet = 0;
    if (!culled)
    {
        HLSL::Mesh::LOD lod = mesh.LODs[lodIndex];
        range.meshletPrefix = lod.meshletCount;
        range.firstMeshlet = lod.meshletOffset;
    }
    instanceMeshletRanges[instanceIndex] = range;
}

// ---- Exclusive prefix over the per-instance meshlet counts ----
// Single group: each thread serially sums one contiguous chunk of instances, the chunk totals are
// scanned in groupshared, then each thread rewrites its chunk as exclusive prefixes. Also publishes
// the total as counter[1], the meshlet count CountMeshletsDispatch / CullingMeshlets work from.

#define INSTANCE_PREFIX_THREADS 1024

groupshared uint gsChunkPrefix[INSTANCE_PREFIX_THREADS];

#pragma compute InstancePrefix InstanceMeshletPrefix

[RootSignature(SeeDRootSignature)]
[numthreads(INSTANCE_PREFIX_THREADS, 1, 1)]
void InstanceMeshletPrefix(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    RWStructuredBuffer<HLSL::InstanceMeshletRange> instanceMeshletRanges = ResourceDescriptorHeap[viewContext.instanceMeshletRangesIndex];
    RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[viewContext.instancesCounterIndex];

    uint instanceCount = commonResourcesIndices.instanceCount;
    uint chunkSize = (instanceCount + INSTANCE_PREFIX_THREADS - 1) / INSTANCE_PREFIX_THREADS;
    uint chunkBegin = min(gtid.x * chunkSize, instanceCount);
    uint chunkEnd = min(chunkBegin + chunkSize, instanceCount);

    uint chunkTotal = 0;
    for (uint i = chunkBegin; i < chunkEnd; i++)
        chunkTotal += instanceMeshletRanges[i].meshletPrefix;

    // inclusive scan of the chunk totals in groupshared (Hillis-Steele, log2(threads) steps)
    gsChunkPrefix[gtid.x] = chunkTotal;
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = 1; offset < INSTANCE_PREFIX_THREADS; offset <<= 1)
    {
        uint add = gtid.x >= offset ? gsChunkPrefix[gtid.x - offset] : 0;
        GroupMemoryBarrierWithGroupSync();
        gsChunkPrefix[gtid.x] += add;
        GroupMemoryBarrierWithGroupSync();
    }
    if (gtid.x == INSTANCE_PREFIX_THREADS - 1)
        counter[1] = gsChunkPrefix[gtid.x];

    uint running = gsChunkPrefix[gtid.x] - chunkTotal; // exclusive
    for (uint j = chunkBegin; j < chunkEnd; j++)
    {
        uint count = instanceMeshletRanges[j].meshletPrefix;
        instanceMeshletRanges[j].meshletPrefix = running;
        running += count;
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
    
    // Owning instance = last one whose exclusive meshlet prefix <= this thread's index. Culled
    // instances (0 meshlets) share their prefix with a later instance, so they never win.
    RWStructuredBuffer<HLSL::InstanceMeshletRange> instanceMeshletRanges = ResourceDescriptorHeap[viewContext.instanceMeshletRangesIndex];
    uint lo = 0;
    uint hi = commonResourcesIndices.instanceCount - 1;
    while (lo < hi)
    {
        uint mid = (lo + hi + 1) / 2;
        if (instanceMeshletRanges[mid].meshletPrefix <= localMeshletIndex)
            lo = mid;
        else
            hi = mid - 1;
    }
    HLSL::InstanceMeshletRange owner = instanceMeshletRanges[lo];
    HLSL::MeshletDraw meshletToCull;
    meshletToCull.instanceIndex = lo;
    meshletToCull.meshletIndex = owner.firstMeshlet + (localMeshletIndex - owner.meshletPrefix);
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[meshletToCull.instanceIndex];
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletToCull.meshletIndex];
    
    float4x4 worldMatrix = instance.unpack(instance.current);
    // Same CPU-computed world-space override as CullingInstances: when present, terrain gets
    // per-instance/per-node culling granularity here rather than per-meshlet (every meshlet of one
    // leaf node shares that node's sphere) -- terrain patches are already coarse, see terrainmesh.hlsl.
    float3 meshletCenter = mul(worldMatrix, float4(meshlet.boundingSphere.xyz, 1)).xyz;
    float meshletRadius = abs(max(max(length(worldMatrix[0].xyz), length(worldMatrix[1].xyz)), length(worldMatrix[2].xyz)) * meshlet.boundingSphere.w); // assume uniform scaling
    float4 boundingSphere = (instance.boundingSphereOverride.w > 0) ? instance.boundingSphereOverride : float4(meshletCenter, meshletRadius);

    bool culled = FrustumCulling(camera, boundingSphere);
    if (!culled)  culled = OcclusionCulling(camera, boundingSphere);

    // Backface cluster culling with the meshopt normal cone, for single-sided materials only (they
    // also draw with CULL_BACK, see GBuffers::Render). Skipped for override instances (terrain is
    // displaced in the mesh shader, so the flat grid's cones don't apply) and mirrored instances
    // (a negative determinant flips the winding the cone was built for). A cone too wide to ever
    // cull has cutoff 1, for which the test below can never pass.
    if (!culled && instance.boundingSphereOverride.w <= 0)
    {
        StructuredBuffer<HLSL::Material> coneMaterials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
        bool doubleSided = coneMaterials[instance.materialIndex].parameters[HLSL::MaterialDoubleSidedParam] != 0;
        float3x3 rotation = (float3x3)worldMatrix;
        if (!doubleSided && determinant(rotation) > 0)
        {
            float3 coneAxis = normalize(mul(rotation, meshlet.cone.xyz)); // assume uniform scaling
            float3 toCenter = meshletCenter - camera.worldPos.xyz;
            culled = dot(toCenter, coneAxis) >= meshlet.cone.w * length(toCenter) + meshletRadius;
        }
    }

    if (!culled)
    {
        RWStructuredBuffer<uint> meshletCounter = ResourceDescriptorHeap[viewContext.meshletsCounterIndex];

        HLSL::MeshletDraw mdc;
        mdc.instanceIndex = meshletToCull.instanceIndex;
        mdc.meshletIndex = meshletToCull.meshletIndex;

        // Route this meshlet to its material's shader bucket -- a stable per-Shader-asset index
        // assigned by the CPU-side registry (Renderer.h ShaderBucketRegistry::GetOrRegister) and
        // written into HLSL::Material.shaderIndex by UpdateMaterials. Only bucket 0 is ever
        // sort-eligible below (whichever shader happens to claim it first); every other bucket draws
        // unsorted from its own exact region of the single shared meshletsCulledArgs buffer.
        StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
        HLSL::Material meshletMaterial = materials[instance.materialIndex];
        uint shaderBucket = meshletMaterial.shaderIndex;
        // ~0 means the material has no (resolvable) shader assigned -- same "not assigned"
        // convention as HLSL::Material's texture slots. Don't draw it, same as a culled meshlet,
        // rather than falling back to some default.
        if (shaderBucket == ~0)
            return;

        StructuredBuffer<uint> shaderBucketOffsets = ResourceDescriptorHeap[viewContext.shaderBucketOffsetsIndex];
        uint bucketBase = shaderBucketOffsets[shaderBucket];
        // Region size is the CPU's per-bucket meshlet total. A material re-routed at runtime (shader or
        // sidedness edited) sends meshlets to a bucket that wasn't sized for them: drop those rather
        // than overflow into the next bucket's region (BuildMeshDispatchArgs clamps the count too).
        uint bucketCapacity = shaderBucketOffsets[shaderBucket + 1] - bucketBase;

        RWStructuredBuffer<HLSL::MeshletDraw> meshletsCulledArgs = ResourceDescriptorHeap[viewContext.meshletsCulledArgsIndex];
        // Each active lane writes one draw call. A single wave can hold lanes belonging to DIFFERENT
        // shader buckets, so the reservation has to be per-bucket, not per-wave: collapse the lanes
        // sharing a bucket (same WaveMatch ballot the sort histogram/scatter use), let their lowest
        // lane do one atomic on THAT bucket's counter, and give each lane its slot within that group.
        // Reserving for the whole wave off lane 0's bucket would hand a foreign bucket's offset to
        // every other lane (writing outside its own region, and never bumping its counter).
        uint repLane, bucketLanes;
        uint4 sameBucket = WaveBucketBallot(shaderBucket, repLane, bucketLanes);
        uint rank = WaveMultiPrefixCountBits(true, sameBucket); // active lanes before me sharing this bucket
        uint waveBase = 0;
        if (WaveGetLaneIndex() == repLane)
            InterlockedAdd(meshletCounter[shaderBucket], bucketLanes, waveBase);
        waveBase = WaveReadLaneAt(waveBase, repLane);
        if (waveBase + rank >= bucketCapacity)
            return;
        uint index = bucketBase + waveBase + rank;
        meshletsCulledArgs[index] = mdc;

        // Front-to-back sort only ever applies to bucket 0 (the default opaque shader): its region
        // always starts at buffer offset 0 by construction (first in the prefix sum), so
        // DrawSortPrefix/DrawSortScatter (which operate on meshletCounter[0] / offset 0
        // unconditionally) need no changes at all.
        if (shaderBucket == 0 && viewContext.frontToBackSort)
        {
            // We already have this meshlet's world-space center (boundingSphere.xyz, whether from the
            // override or the mesh-derived path), so derive its depth bucket here for free. Stash it
            // for the scatter pass and accumulate the histogram.
            uint depthBucket = DepthBucketFromCenter(boundingSphere.xyz, camera);
            RWStructuredBuffer<uint> meshletBuckets = ResourceDescriptorHeap[viewContext.meshletBucketsIndex];
            meshletBuckets[index] = depthBucket;

            RWStructuredBuffer<uint> sortHistogram = ResourceDescriptorHeap[viewContext.sortHistogramIndex];
            // Wave-aggregate the histogram: lanes sharing a bucket collapse into a single global
            // atomic (one per distinct bucket per wave) instead of one atomic per meshlet.
            uint depthRepLane, depthLanes;
            WaveBucketBallot(depthBucket, depthRepLane, depthLanes);
            if (WaveGetLaneIndex() == depthRepLane)
            {
                uint sortBase;
                InterlockedAdd(sortHistogram[depthBucket], depthLanes, sortBase);
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
    StructuredBuffer<uint> shaderBucketOffsets = ResourceDescriptorHeap[viewContext.shaderBucketOffsetsIndex];
    if (dtid.x >= min(meshletCounter[0], shaderBucketOffsets[1])) // bucket 0 starts at 0, so offsets[1] is its capacity
        return;

    RWStructuredBuffer<HLSL::MeshletDraw> meshletsCulledArgs = ResourceDescriptorHeap[viewContext.meshletsCulledArgsIndex];
    RWStructuredBuffer<HLSL::MeshletDraw> meshletsCulledArgsSorted = ResourceDescriptorHeap[viewContext.meshletsCulledArgsSortedIndex];
    RWStructuredBuffer<uint> meshletBuckets = ResourceDescriptorHeap[viewContext.meshletBucketsIndex];
    RWStructuredBuffer<uint> sortHistogram = ResourceDescriptorHeap[viewContext.sortHistogramIndex];

    HLSL::MeshletDraw mdc = meshletsCulledArgs[dtid.x];
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

// ---- GBuffer dispatch args: one DispatchMesh per shader bucket ----
// Runs after CullingMeshlets has settled every bucket's counter. Each bucket's culled meshlets are
// drawn as one 2D grid of mesh-shader groups (HLSL::meshDispatchWidth wide) instead of one indirect
// command per meshlet; the mesh shader maps its flattened group id back to a MeshletDraw entry.

#pragma compute BuildMeshDispatch BuildMeshDispatchArgs

[RootSignature(SeeDRootSignature)]
[numthreads(64, 1, 1)]
void BuildMeshDispatchArgs(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint bucket = dtid.x;
    if (bucket >= viewContext.shaderBucketCount)
        return;

    RWStructuredBuffer<uint> meshletCounter = ResourceDescriptorHeap[viewContext.meshletsCounterIndex];
    RWStructuredBuffer<HLSL::MeshDispatchArgs> meshDispatchArgs = ResourceDescriptorHeap[viewContext.meshDispatchArgsIndex];

    StructuredBuffer<uint> shaderBucketOffsets = ResourceDescriptorHeap[viewContext.shaderBucketOffsetsIndex];
    uint capacity = shaderBucketOffsets[bucket + 1] - shaderBucketOffsets[bucket];
    uint count = min(min(meshletCounter[bucket], capacity), HLSL::meshDispatchMaxGroups);

    HLSL::MeshDispatchArgs args;
    args.meshletCount = count;
    args.ThreadGroupCountX = min(count, HLSL::meshDispatchWidth);
    args.ThreadGroupCountY = (count + HLSL::meshDispatchWidth - 1) / HLSL::meshDispatchWidth;
    args.ThreadGroupCountZ = 1;
    meshDispatchArgs[bucket] = args;
}
