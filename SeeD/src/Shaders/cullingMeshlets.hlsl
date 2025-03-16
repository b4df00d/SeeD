#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CullingMeshlet

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
        camera = cameras[cullingContext.cameraIndex];
    }
    GroupMemoryBarrierWithGroupSync();
    
    uint localMeshletIndex = dtid.x;
    if (localMeshletIndex >= mesh.meshletCount)
        return;
    
    uint meshletIndex = mesh.meshletOffset + localMeshletIndex;
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletIndex];
    
    
    float4x4 worldMatrix = instance.unpack();
    float3 center = mul(worldMatrix, float4(meshlet.boundingSphere.xyz, 1)).xyz;
    float radius = abs(max(max(length(instance.matA.xyz), length(instance.matA.xyz)), length(instance.matC.xyz)) * meshlet.boundingSphere.w); // assume uniform scaling
    float4 boundingSphere = float4(center, radius);
    
    bool culled = FrustumCulling(camera, boundingSphere);
    if (!culled)  culled = OcclusionCulling(camera, boundingSphere);
    
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