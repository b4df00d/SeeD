#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma gBuffer AmplificationMain MeshMain PixelgBuffer
//#pragma forward AmplificationMain MeshMain PixelgBuffer

struct Payload
{
    uint instanceIndex[128];
    uint meshletIndices[128];
};

groupshared Payload sPayload;
groupshared uint payloadIndex;

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void AmplificationMain(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
    uint instanceIndex = dtid.x;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cullingContext.cameraIndex];
    
    uint meshletCount = min(256, mesh.meshletCount);
    
    float4x4 worldMatrix = instance.unpack();
    float4 boundingSphere = mul(worldMatrix, float4(mesh.boundingSphere.xyz, 1));
    boundingSphere.w = mesh.boundingSphere.w;
    
    bool culled = FrustumCulling(camera, boundingSphere);
    
    if (culled)  meshletCount = 0;
    
    
    payloadIndex = 0;
    uint index = 0;
    for (uint i = 0; i < meshletCount; i++)
    {
        
        StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
        HLSL::Meshlet meshlet = meshlets[mesh.meshletOffset + i];
        
        boundingSphere = mul(worldMatrix, float4(meshlet.boundingSphere.xyz, 1));
        boundingSphere.w = mesh.boundingSphere.w;
    
        culled = FrustumCulling(camera, boundingSphere);
    
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

[RootSignature(SeeDRootSignature)]
[outputtopology("triangle")]
[numthreads(HLSL::max_triangles, 1, 1)]
void MeshMain(in uint3 groupId : SV_GroupID, in uint3 groupThreadId : SV_GroupThreadID, in payload Payload payload, out vertices HLSL::MSVert outVerts[HLSL::max_vertices], out indices uint3 outIndices[HLSL::max_triangles])
{
    StructuredBuffer<HLSL:: Camera > cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[0]; //cullingContext.cameraIndex];
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    
    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    HLSL::Meshlet meshlet = meshlets[meshletIndexIndirect];
    meshlet.vertexCount = min(HLSL::max_vertices, meshlet.vertexCount);
    meshlet.triangleCount = min(HLSL::max_triangles, meshlet.triangleCount);
    
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);
    
    StructuredBuffer<uint> meshletVertices = ResourceDescriptorHeap[commonResourcesIndices.meshletVerticesHeapIndex];
    StructuredBuffer<HLSL::Vertex> verticesData = ResourceDescriptorHeap[commonResourcesIndices.verticesHeapIndex];
    if (groupThreadId.x < meshlet.vertexCount)
    {
        float4x4 worldMatrix = instance.unpack();
        uint tmpIndex = meshlet.vertexOffset + groupThreadId.x;
        uint index = meshletVertices[tmpIndex];
        float4 pos = float4(verticesData[index].pos.xyz, 1);
        float4 worldPos = mul(worldMatrix, pos);
        float3 normal = verticesData[index].normal.xyz;
        float3 worldNormal = mul((float3x3)worldMatrix, normal);
        outVerts[groupThreadId.x].pos = mul(camera.viewProj, worldPos);
        outVerts[groupThreadId.x].normal = worldNormal;
        outVerts[groupThreadId.x].color = RandUINT(meshletIndexIndirect);
        outVerts[groupThreadId.x].uv = verticesData[index].uv;
    }
    ByteAddressBuffer trianglesData = ResourceDescriptorHeap[commonResourcesIndices.meshletTrianglesHeapIndex]; // because of uint8 format
    if (groupThreadId.x < meshlet.triangleCount)
    {
        uint offset = meshlet.triangleOffset + groupThreadId.x * 3;
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
        
        outIndices[groupThreadId.x] = uint3(tri[diff], tri[diff + 1], tri[diff + 2]);
    }
}

struct PS_OUTPUT_FORWARD
{
    float4 albedo : SV_Target0;
    float3 normal : SV_Target1;
    //uint entityID : SV_Target1;
};

PS_OUTPUT_FORWARD PixelForward(HLSL::MSVert inVerts)
{
    PS_OUTPUT_FORWARD o;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    o.albedo = float4(inVerts.color, 1);
    o.normal = inVerts.normal.xyz;
    //o.entityID = 1;
    return o;
}

PS_OUTPUT_FORWARD PixelgBuffer(HLSL::MSVert inVerts)
{
    PS_OUTPUT_FORWARD o;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    
    StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instance.materialIndex];
    
    Texture2D<float4> albedo = ResourceDescriptorHeap[material.textures[0]];
    
    o.albedo = albedo.Sample(samplerLinear, inVerts.uv);
    //o.albedo = float4(inVerts.color, 1);
    o.normal = inVerts.normal.xyz;
    //o.entityID = 1;
    return o;
}