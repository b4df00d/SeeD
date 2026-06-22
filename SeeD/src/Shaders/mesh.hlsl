#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

// define the output of shader before the call to shader so that the parser can know it before compiling
// the comment after the SV_target is important
struct PS_OUTPUT
{
    float4 albedo : SV_Target0; //DXGI_FORMAT_R8G8B8A8_UNORM
    float4 specularAlbedo : SV_Target1; //DXGI_FORMAT_R8G8B8A8_UNORM
    float4 normal : SV_Target2; //DXGI_FORMAT_R16G16B32A32_FLOAT
    float metalness : SV_Target3; //DXGI_FORMAT_R8_UNORM
    float roughness : SV_Target4; //DXGI_FORMAT_R8_UNORM
    float2 motion : SV_Target5; //DXGI_FORMAT_R16G16_FLOAT
    uint objectID : SV_Target6; //DXGI_FORMAT_R32_UINT
    uint instanceID : SV_Target7; //DXGI_FORMAT_R32_UINT
};

#pragma gBuffer DefaultG MeshMain PixelgBuffer
#pragma forward DefaultF MeshMain PixelForward

struct MSVert
{
    float4 pos : SV_Position;
    float4 currentPos : TEXCOORD0;
    float4 previousPos : TEXCOORD1;
    float3 normal : NORMAL;
    float4 tangent : TEXCOORD2; // xyz = tangent, w = world-space handedness (binormal sign)
    float2 uv : TEXCOORD3;
};

groupshared HLSL::Mesh mesh;
groupshared HLSL::Meshlet meshlet;
groupshared float4x4 worldMatrix;     // object->world (for normal/tangent)
groupshared float4x4 mvp;             // viewProj * world
groupshared float4x4 previousMvp;     // previousViewProj * previousWorld
groupshared float worldDetSign;       // sign(det(world)) -> keeps binormal handedness correct on mirrored instances

[RootSignature(SeeDRootSignature)]
[outputtopology("triangle")]
[numthreads(HLSL::max_triangles, 1, 1)]
void MeshMain(in uint3 groupId : SV_GroupID, in uint3 groupThreadId : SV_GroupThreadID, out vertices MSVert outVerts[HLSL::max_vertices], out indices uint3 outIndices[HLSL::max_triangles])
{   
    if(groupThreadId.x == 0)
    {
        StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
        HLSL::Camera camera = cameras[0]; //viewContext.cameraIndex];

        StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
        HLSL::Instance instance = instances[instanceIndexIndirect];
        worldMatrix = instance.unpack(instance.current);
        float4x4 previousWorldMatrix = instance.unpack(instance.previous);

        // Concatenate once per group so each vertex does a single matrix*vector (no intermediate worldPos).
        mvp = mul(camera.viewProj, worldMatrix);
        previousMvp = mul(camera.previousViewProj, previousWorldMatrix);
        worldDetSign = determinant((float3x3)worldMatrix) >= 0.0f ? 1.0f : -1.0f;

        StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
        mesh = meshes[instance.meshIndex]; // for SNORM16 position decode (aabbMin / aabbExtent)

        StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
        meshlet = meshlets[meshletIndexIndirect];
        meshlet.vertexCount = min(HLSL::max_vertices, meshlet.vertexCount);
        meshlet.triangleCount = min(HLSL::max_triangles, meshlet.triangleCount);
    }
    GroupMemoryBarrierWithGroupSync();
    
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);
    
    StructuredBuffer<uint> meshletVertices = ResourceDescriptorHeap[commonResourcesIndices.meshletVerticesHeapIndex];
    StructuredBuffer<HLSL::Vertex> verticesData = ResourceDescriptorHeap[commonResourcesIndices.verticesHeapIndex];
    if (groupThreadId.x < meshlet.vertexCount)
    {
        uint index = meshletVertices[meshlet.vertexOffset + groupThreadId.x];
        HLSL::Vertex v = verticesData[index]; // single fetch

        // Decode SNORM16 position (local to mesh AABB). packedPos.x = [x|y<<16], packedPos.y = [z|handedness<<16].
        int3 qi = int3(int(v.packedPos.x << 16) >> 16, int(v.packedPos.x) >> 16, int(v.packedPos.y << 16) >> 16);
        float3 q = max(float3(qi) / 32767.0f, -1.0f);
        float3 objectPos = mesh.aabbMin.xyz + (q * 0.5f + 0.5f) * mesh.aabbExtent.xyz;
        float4 pos = float4(objectPos, 1);

        float4 clipPos = mul(mvp, pos);
        outVerts[groupThreadId.x].currentPos = clipPos;
        clipPos.xy += viewContext.jitter.xy * clipPos.w;
        outVerts[groupThreadId.x].pos = clipPos;

        outVerts[groupThreadId.x].previousPos = mul(previousMvp, pos);

        float3 normal = i_octahedral_32(v.normalOct, 16);
        outVerts[groupThreadId.x].normal = normalize(mul((float3x3)worldMatrix, normal));

        // Pass tangent + world-space handedness; the binormal is rebuilt in the pixel shader.
        float3 tangent = i_octahedral_32(v.tangentOct, 16);
        float handedness = ((int(v.packedPos.y) >> 16) >= 0 ? 1.0f : -1.0f) * worldDetSign;
        outVerts[groupThreadId.x].tangent = float4(mul((float3x3)worldMatrix, tangent), handedness);

        outVerts[groupThreadId.x].uv = v.uv;
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

    float3 binormal = cross(inVerts.normal, inVerts.tangent.xyz) * inVerts.tangent.w;
    SurfaceData s = GetSurfaceData(material, inVerts.uv, inVerts.normal, inVerts.tangent.xyz, binormal);

    o.albedo = s.albedo;
    o.specularAlbedo = lerp(1, s.albedo, s.metalness);
    o.roughness = s.roughness;
    o.metalness = s.metalness;
    o.normal = float4(StoreNormal(normalize(s.normal)), 1);

    o.motion = CalcVelocity(inVerts.currentPos, inVerts.previousPos, viewContext.renderResolution.xy);

    o.objectID = instance.objectID;
    o.instanceID = instanceIndexIndirect;

    return o;
}


// Force early depth test so occluded fragments are rejected before the shader runs.
// Without this, the overdraw UAV write below makes D3D12 default to late depth testing,
// which shades every covered fragment regardless of draw order (defeating the front-to-back sort).
[earlydepthstencil]
PS_OUTPUT PixelgBuffer(MSVert inVerts)
{
    PS_OUTPUT o;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndexIndirect];
    
    StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instance.materialIndex];

    float3 binormal = cross(inVerts.normal, inVerts.tangent.xyz) * inVerts.tangent.w;
    SurfaceData s = GetSurfaceData(material, inVerts.uv, inVerts.normal, inVerts.tangent.xyz, binormal);

    o.albedo = s.albedo;

    if (editorContext.clusters)
    {
        o.albedo = 1;
        o.albedo.xyz = RandUINT(meshletIndexIndirect); // per-meshlet debug color (computed here, not interpolated)
    }
    
    if((o.albedo.a+0.01) < material.parameters[4]) discard;

    if (editorContext.overdraw) // count shaded fragments per pixel for the overdraw heatmap
    {
        RWTexture2D<uint> overdraw = ResourceDescriptorHeap[viewContext.overdrawIndex];
        uint previous;
        InterlockedAdd(overdraw[uint2(inVerts.pos.xy)], 1, previous);
    }

    o.specularAlbedo = lerp(1, s.albedo, s.metalness);
    o.roughness = s.roughness;
    o.metalness = s.metalness;
    o.normal = float4(StoreNormal(normalize(s.normal)), 1);
    
    o.motion = CalcVelocity(inVerts.currentPos, inVerts.previousPos, viewContext.renderResolution.xy);

    o.objectID = instance.objectID;
    o.instanceID = instanceIndexIndirect;

    return o;
}


/*
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
*/