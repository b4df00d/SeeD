#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"
#include "meshCommon.hlsli"

// define the output of shader before the call to shader so that the parser can know it before compiling
// the comment after the SV_target is important
struct PS_OUTPUT
{
    float4 albedo : SV_Target0; //DXGI_FORMAT_R8G8B8A8_UNORM
    float4 specularAlbedo : SV_Target1; //DXGI_FORMAT_R8G8B8A8_UNORM
    float4 normal : SV_Target2; //DXGI_FORMAT_R16G16B16A16_FLOAT (a = roughness, DLSS-RR packed mode)
    float metalness : SV_Target3; //DXGI_FORMAT_R8_UNORM
    float2 motion : SV_Target4; //DXGI_FORMAT_R16G16_FLOAT
    uint instanceID : SV_Target5; //DXGI_FORMAT_R32_UINT
};

#pragma gBuffer DefaultG MeshMain PixelgBuffer
#pragma gBuffer DefaultGCutout MeshMain PixelgBuffer CUTOUT
#pragma forward DefaultF MeshMain PixelForward

// MSVert/MSPrim, the groupshared meshlet/instance state, and the fetch/decode/output helpers
// (LoadMeshletShared, DecodeVertex*, BuildOutputVertex, DecodeMeshletTriangle) live in
// meshCommon.hlsli, shared with terrainmesh.hlsl (Milestone 1 terrain, plan step 6).

[RootSignature(SeeDRootSignature)]
[outputtopology("triangle")]
[numthreads(HLSL::max_triangles, 1, 1)]
void MeshMain(in uint3 groupId : SV_GroupID, in uint3 groupThreadId : SV_GroupThreadID, out vertices MSVert outVerts[HLSL::max_vertices], out indices uint3 outIndices[HLSL::max_triangles], out primitives MSPrim outPrims[HLSL::max_triangles])
{
    LoadMeshletShared(groupThreadId.x);
    GroupMemoryBarrierWithGroupSync();

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    StructuredBuffer<uint> meshletVertices = ResourceDescriptorHeap[commonResourcesIndices.meshletVerticesHeapIndex];
    StructuredBuffer<HLSL::Vertex> verticesData = ResourceDescriptorHeap[commonResourcesIndices.verticesHeapIndex];
    if (groupThreadId.x < meshlet.vertexCount)
    {
        uint index = meshletVertices[meshlet.vertexOffset + groupThreadId.x];
        HLSL::Vertex v = verticesData[index + mesh.vertexOffset];

        float3 objectPos = DecodeVertexPositionOS(v);
        float3 normalOS = DecodeVertexNormalOS(v);
        float3 tangentOS = DecodeVertexTangentOS(v);
        // Pass tangent + world-space handedness; the binormal is rebuilt in the pixel shader.
        float handedness = DecodeVertexHandedness(v);

        outVerts[groupThreadId.x] = BuildOutputVertex(objectPos, normalOS, tangentOS, handedness, v.uv);
    }
    if (groupThreadId.x < meshlet.triangleCount)
    {
        outIndices[groupThreadId.x] = DecodeMeshletTriangle(groupThreadId.x);
        outPrims[groupThreadId.x].primitiveID = groupThreadId.x;
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
    o.metalness = s.metalness;
    // normal.a carries roughness (DLSS-RR packed mode; lighting.hlsl preserves it on its rewrite)
    o.normal = float4(StoreNormal(normalize(s.normal)), s.roughness);

    o.motion = CalcVelocity(inVerts.currentPos, inVerts.previousPos, viewContext.renderResolution.xy);

    o.instanceID = instanceIndexIndirect; // objectID is derived from this in selection.hlsl

    return o;
}


// Force early depth test so occluded fragments are rejected before the shader runs.
// Without this, the overdraw UAV write below makes D3D12 default to late depth testing,
// which shades every covered fragment regardless of draw order (defeating the front-to-back sort).
// Cutout variant (CUTOUT define) omits early-Z so the alpha-test discard runs BEFORE depth is
// written -- otherwise the transparent texels would still occlude what's behind them.
#ifndef CUTOUT
[earlydepthstencil]
#endif
PS_OUTPUT PixelgBuffer(MSVert inVerts, uint primitiveID : SV_PrimitiveID)
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
    if (editorContext.triangles)
    {
        o.albedo = 1;
        o.albedo.xyz = RandUINT(meshletIndexIndirect * HLSL::max_triangles + primitiveID); // unique seed per triangle
    }
    
#ifdef CUTOUT
    if((o.albedo.a+0.01) < material.parameters[4]) discard;
#endif

    if (editorContext.overdraw) // count shaded fragments per pixel for the overdraw heatmap
    {
        RWTexture2D<uint> overdraw = ResourceDescriptorHeap[viewContext.overdrawIndex];
        uint previous;
        InterlockedAdd(overdraw[uint2(inVerts.pos.xy)], 1, previous);
    }

    o.specularAlbedo = lerp(1, s.albedo, s.metalness);
    o.metalness = s.metalness;
    // normal.a carries roughness (DLSS-RR packed mode; lighting.hlsl preserves it on its rewrite)
    o.normal = float4(StoreNormal(normalize(s.normal)), s.roughness);
    
    o.motion = CalcVelocity(inVerts.currentPos, inVerts.previousPos, viewContext.renderResolution.xy);

    o.instanceID = instanceIndexIndirect; // objectID is derived from this in selection.hlsl

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