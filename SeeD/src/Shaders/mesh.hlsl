#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

// define the output of shader before the call to shader so that the parser can know it before compiling
// the comment after the SV_target is important
struct PS_OUTPUT
{
    float4 albedo : SV_Target0; //DXGI_FORMAT_R8G8B8A8_UNORM
    float4 specularAlbedo : SV_Target1; //DXGI_FORMAT_R8G8B8A8_UNORM
    float3 normal : SV_Target2; //DXGI_FORMAT_R11G11B10_FLOAT
    float metalness : SV_Target3; //DXGI_FORMAT_R8_UNORM
    float roughness : SV_Target4; //DXGI_FORMAT_R8_UNORM
    float2 motion : SV_Target5; //DXGI_FORMAT_R16G16_FLOAT
    uint objectID : SV_Target6; //DXGI_FORMAT_R32_UINT
};

#pragma gBuffer AmplificationMain MeshMain PixelgBuffer
//#pragma forward AmplificationMain MeshMain PixelgBuffer

struct MSVert
{
    float4 pos : SV_Position;
    float4 currentPos : TEXCOORD0;
    float4 previousPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 tangent : TEXCOORD2;
    float3 binormal : TEXCOORD3;
    float3 color : COLOR0;
    float2 uv : TEXCOORD4;
    nointerpolation uint objectID : TEXCOORD5;
};

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
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    uint meshletCount = min(512, mesh.meshletCount);
    
    float4x4 worldMatrix = instance.unpack(instance.current);
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
void MeshMain(in uint3 groupId : SV_GroupID, in uint3 groupThreadId : SV_GroupThreadID, in payload Payload payload, out vertices MSVert outVerts[HLSL::max_vertices], out indices uint3 outIndices[HLSL::max_triangles])
{
    StructuredBuffer<HLSL:: Camera > cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[0]; //viewContext.cameraIndex];
    
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
        uint tmpIndex = meshlet.vertexOffset + groupThreadId.x;
        uint index = meshletVertices[tmpIndex];
        float4 pos = float4(verticesData[index].pos.xyz, 1);
        
        float4x4 worldMatrix = instance.unpack(instance.current);
        float4 worldPos = mul(worldMatrix, pos);
        float4 clipPos = mul(camera.viewProj, worldPos);
        outVerts[groupThreadId.x].currentPos = clipPos;
        clipPos.xy += viewContext.jitter.xy * clipPos.w;
        outVerts[groupThreadId.x].pos = clipPos;
        
        float4x4 previousWorldMatrix = instance.unpack(instance.previous);
        float4 previousWorldPos = mul(previousWorldMatrix, pos);
        float4 previousClipPos = mul(camera.previousViewProj, previousWorldPos);
        outVerts[groupThreadId.x].previousPos = previousClipPos;
        
        float3 normal = verticesData[index].normal.xyz;
        float3 worldNormal = mul((float3x3)worldMatrix, normal);
        outVerts[groupThreadId.x].normal = normalize(worldNormal);
        outVerts[groupThreadId.x].tangent = 0;// TODO : compute those 
        outVerts[groupThreadId.x].binormal = 0;// TODO : compute those 
        
        outVerts[groupThreadId.x].color = RandUINT(meshletIndexIndirect);
        outVerts[groupThreadId.x].uv = verticesData[index].uv;
        
        outVerts[groupThreadId.x].objectID = instance.objectID;
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

PS_OUTPUT PixelForward(MSVert inVerts)
{
    PS_OUTPUT o;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    
    StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instance.materialIndex];
    
    SurfaceData s = GetSurfaceData(material, inVerts.uv, inVerts.normal, inVerts.tangent, inVerts.binormal);
    
    o.albedo = s.albedo;
    o.specularAlbedo = lerp(1, s.albedo, s.metalness);
    o.roughness = s.roughness;
    o.metalness = s.metalness;
    o.normal = StoreR11G11B10Normal(normalize(s.normal));
    
    o.motion = CalcVelocity(inVerts.currentPos, inVerts.previousPos, viewContext.renderResolution.xy);
    
    o.objectID = inVerts.objectID;
    
    return o;
}


PS_OUTPUT PixelgBuffer(MSVert inVerts)
{
    PS_OUTPUT o;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    
    StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instance.materialIndex];
    
    SurfaceData s = GetSurfaceData(material, inVerts.uv, inVerts.normal, inVerts.tangent, inVerts.binormal);
    
    o.albedo = s.albedo;
    
    if(o.albedo.a<=0.05) discard;
    
    o.specularAlbedo = lerp(1, s.albedo, s.metalness);
    o.roughness = s.roughness;
    o.metalness = s.metalness;
    o.normal = StoreR11G11B10Normal(normalize(s.normal));
    
    o.motion = CalcVelocity(inVerts.currentPos, inVerts.previousPos, viewContext.renderResolution.xy);
    
    o.objectID = inVerts.objectID;
    
    return o;
}