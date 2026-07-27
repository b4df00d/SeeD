#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"
#include "meshCommon.hlsli"

struct PS_OUTPUT
{
    float4 albedo : SV_Target0; //DXGI_FORMAT_R8G8B8A8_UNORM
    float4 specularAlbedo : SV_Target1; //DXGI_FORMAT_R8G8B8A8_UNORM
    float4 normal : SV_Target2; //DXGI_FORMAT_R16G16B16A16_FLOAT (a = roughness, DLSS-RR packed mode)
    float metalness : SV_Target3; //DXGI_FORMAT_R8_UNORM
    float2 motion : SV_Target4; //DXGI_FORMAT_R16G16_FLOAT
    uint instanceID : SV_Target5; //DXGI_FORMAT_R32_UINT
};

// Routed as its own late-Z GBuffer draw (3rd bucket, see culling.hlsl / Renderer.h GBuffers::Render)
// -- there's no alpha discard here, but keeping early-Z off costs nothing extra since terrain draws
// are a handful of large patches, not a mainstay of overdraw.
#pragma gBuffer DefaultGTerrain MeshMainTerrain PixelgBufferTerrain

[RootSignature(SeeDRootSignature)]
[outputtopology("triangle")]
[numthreads(HLSL::max_triangles, 1, 1)]
void MeshMainTerrain(in uint3 groupId : SV_GroupID, in uint3 groupThreadId : SV_GroupThreadID, out vertices MSVert outVerts[HLSL::max_vertices], out indices uint3 outIndices[HLSL::max_triangles], out primitives MSPrim outPrims[HLSL::max_triangles])
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
        float3 tangentOS = DecodeVertexTangentOS(v);
        float handedness = DecodeVertexHandedness(v);
        float3 normalOS = DecodeVertexNormalOS(v);

        outVerts[groupThreadId.x] = BuildOutputVertex(objectPos, normalOS, tangentOS, handedness, v.uv);
    }
    if (groupThreadId.x < meshlet.triangleCount)
    {
        outIndices[groupThreadId.x] = DecodeMeshletTriangle(groupThreadId.x);
        outPrims[groupThreadId.x].primitiveID = groupThreadId.x;
    }
}

// Same as mesh.hlsl's opaque PixelgBuffer (no CUTOUT path -- terrain has no alpha discard).
[earlydepthstencil]
PS_OUTPUT PixelgBufferTerrain(MSVert inVerts, uint primitiveID : SV_PrimitiveID)
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
