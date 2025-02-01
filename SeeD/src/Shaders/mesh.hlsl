#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

//#pragma gBuffer MeshMain PixelgBuffer
#pragma forward AmplificationMain MeshMain PixelForward

struct Payload
{
    uint instanceIndex[128];
    uint meshletIndices[128];
};

groupshared Payload sPayload;
groupshared uint payloadIndex;

[RootSignature(GlobalRootSignature)]
[numthreads(1, 1, 1)]
void AmplificationMain(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
    uint instanceIndex = dtid;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cullingContext.cameraIndex];
    
    uint meshletCount = min(256, mesh.meshletCount);
    
    float4 boundingSphere = mul(instance.worldMatrix, float4(mesh.boundingSphere.xyz, 1));
    boundingSphere.w = mesh.boundingSphere.w;
    
    bool culled = FrustumCulling(camera.planes, boundingSphere);
    
    if (culled)  meshletCount = 0;
    
    
    payloadIndex = 0;
    uint index = 0;
    for (uint i = 0; i < meshletCount; i++)
    {
        
        StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
        HLSL::Meshlet meshlet = meshlets[mesh.meshletOffset + i];
        
        boundingSphere = mul(instance.worldMatrix, float4(meshlet.boundingSphere.xyz, 1));
        boundingSphere.w = mesh.boundingSphere.w;
    
        culled = FrustumCulling(camera.planes, boundingSphere);
    
        if (!culled)
        {
            InterlockedAdd(payloadIndex, 1, index);
            if (instanceIndex >= commonResourcesIndices.instanceCount) 
                instanceIndex = HLSL::invalidUINT;
            sPayload.instanceIndex[index] = instanceIndex;
            sPayload.meshletIndices[index] = mesh.meshletOffset + i;
        }
    }
    
    DispatchMesh(min(256, payloadIndex), 1, 1, sPayload);
}

[RootSignature(GlobalRootSignature)]
[outputtopology("triangle")]
[numthreads(124, 1, 1)]
void MeshMain(in uint groupId : SV_GroupID, in uint groupThreadId : SV_GroupThreadID, in payload Payload payload, out vertices HLSL::MSVert outVerts[64], out indices uint3 outIndices[124])
{
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletIndex];
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cullingContext.cameraIndex];
    
    StructuredBuffer<uint> meshletVertices = ResourceDescriptorHeap[commonResourcesIndices.meshletVerticesHeapIndex];
    StructuredBuffer<HLSL::Vertex> verticesData = ResourceDescriptorHeap[commonResourcesIndices.verticesHeapIndex];
    if (groupThreadId < meshlet.vertexCount)
    {
        uint tmpIndex = meshlet.vertexOffset + groupThreadId;
        uint index = meshletVertices[tmpIndex];
        float4 pos = float4(verticesData[index].pos, 1);
        float4 worldPos = mul(instance.worldMatrix, pos);
        outVerts[groupThreadId].pos = mul(camera.viewProj, worldPos);
        outVerts[groupThreadId].color = RandUINT(meshletIndex);
    }
    ByteAddressBuffer trianglesData = ResourceDescriptorHeap[commonResourcesIndices.meshletTrianglesHeapIndex]; // because of uint8 format
    if (groupThreadId < meshlet.triangleCount)
    {
        uint offset = meshlet.triangleOffset + groupThreadId * 3;
        uint alignedOffset = offset & ~3;
        uint diff = (offset - alignedOffset);
        uint2 packedData = trianglesData.Load2(alignedOffset);
        
        uint tri[8];
        tri[0] = (packedData.x) & 0x000000ff;
        tri[1] = (packedData.x >> 8) & 0x000000ff;
        tri[2] = (packedData.x >> 16) & 0x000000ff;
        tri[3] = (packedData.x >> 24) & 0x000000ff;
        
        tri[4] = (packedData.y) & 0x000000ff;
        tri[5] = (packedData.y >> 8) & 0x000000ff;
        tri[6] = (packedData.y >> 16) & 0x000000ff;
        tri[7] = (packedData.y >> 24) & 0x000000ff;
        
        outIndices[groupThreadId] = uint3(tri[diff], tri[diff + 1], tri[diff + 2]);
    }
}

struct PS_OUTPUT_FORWARD
{
    float4 albedo : SV_Target0;
    //uint entityID : SV_Target1;
};

PS_OUTPUT_FORWARD PixelForward(HLSL::MSVert inVerts)
{
    PS_OUTPUT_FORWARD o;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    o.albedo = float4(inVerts.color, 1);
    //o.entityID = 1;
    return o;
}

PS_OUTPUT_FORWARD PixelgBuffer()
{
    PS_OUTPUT_FORWARD o;
    o.albedo = 1;
    //o.entityID = 1;
    return o;
}