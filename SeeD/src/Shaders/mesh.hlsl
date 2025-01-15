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
    //HLSL::Instance instance = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1 * instanceIndex, 1, 1 }, instanceIndex, 0, 0, 0 };
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[mesh.meshletOffset];
    
    payloadIndex = 0;
    uint index = 0;
    uint meshletCount = min(128, mesh.meshletCount);
    for (uint i = 0; i < meshletCount; i++)
    {
        InterlockedAdd(payloadIndex, 1, index);
        if (instanceIndex >= commonResourcesIndices.instanceCount) 
            instanceIndex = HLSL::invalidUINT;
        sPayload.instanceIndex[index] = instanceIndex;
        sPayload.meshletIndices[index] = mesh.meshletOffset + i;
    }
    
    DispatchMesh(min(128, payloadIndex), 1, 1, sPayload);
}

[RootSignature(GlobalRootSignature)]
[outputtopology("triangle")]
[numthreads(124, 1, 1)]
void MeshMain(in uint groupId : SV_GroupID, in uint groupThreadId : SV_GroupThreadID, in payload Payload payload, out vertices HLSL::MSVert outVerts[64], out indices uint3 outIndices[124])
{
    uint instanceIndex = payload.instanceIndex[groupId];
    uint meshletIndex = payload.meshletIndices[groupId];
    
    if (instanceIndex == HLSL::invalidUINT)
        return;
    
    StructuredBuffer<HLSL:: Instance > instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    //HLSL::Instance instance = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1 * instanceIndex, 1, 1 }, instanceIndex, 0, 0, 0 };
    
    StructuredBuffer<HLSL:: Meshlet > meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletIndex];
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cameraIndex];
    
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
    StructuredBuffer<uint> trianglesData = ResourceDescriptorHeap[commonResourcesIndices.meshletTrianglesHeapIndex]; // because of uint8 format
    if (groupThreadId < meshlet.triangleCount)
    {
        uint offset = meshlet.triangleOffset + groupThreadId * 3;
        uint a = trianglesData[offset];
        uint b = trianglesData[offset + 1];
        uint c = trianglesData[offset + 2];
        uint3 abc = uint3(a, b, c);
        outIndices[groupThreadId] = abc;
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