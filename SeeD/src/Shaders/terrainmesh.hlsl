#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"
#include "meshCommon.hlsli"

// Routed as its own late-Z GBuffer draw (3rd bucket, see culling.hlsl / Renderer.h GBuffers::Render)
// -- there's no alpha discard here, but keeping early-Z off costs nothing extra since terrain draws
// are a handful of large patches, not a mainstay of overdraw.
#pragma gBuffer DefaultGTerrain MeshMainTerrain PixelgBufferTerrain

[RootSignature(SeeDRootSignature)]
[outputtopology("triangle")]
[numthreads(HLSL::max_triangles, 1, 1)]
void MeshMainTerrain(in uint3 groupId : SV_GroupID, in uint3 groupThreadId : SV_GroupThreadID, out vertices MSVert outVerts[HLSL::max_vertices], out indices uint3 outIndices[HLSL::max_triangles], out primitives MSPrim outPrims[HLSL::max_triangles])
{
    // Groups past the bucket's count (tail of its 2D dispatch grid) load a clamped, valid entry and
    // output nothing. Branch-free on purpose: the validator requires a single SetMeshOutputCounts
    // call that dominates every output write, and without a barrier the compiler would otherwise
    // duplicate it into an early-out path.
    uint drawIndex = MeshDrawIndex(groupId);
    bool inRange = drawIndex < meshDrawCount;
    LoadMeshlet(min(drawIndex, meshDrawCount - 1));
    if (!inRange)
    {
        meshlet.vertexCount = 0;
        meshlet.triangleCount = 0;
    }

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
        outPrims[groupThreadId.x].instanceIndex = drawInstanceIndex;
        outPrims[groupThreadId.x].meshletIndex = drawMeshletIndex;
    }
}

// Same as mesh.hlsl's opaque PixelgBuffer (no CUTOUT path -- terrain has no alpha discard).
[earlydepthstencil]
PS_OUTPUT PixelgBufferTerrain(MSVert inVerts, MSPrim inPrim)
{
    return GBufferPixel(inVerts, inPrim);
}
