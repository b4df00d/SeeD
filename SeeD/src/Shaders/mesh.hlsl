#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"
#include "meshCommon.hlsli"

#pragma gBuffer DefaultG MeshMain PixelgBuffer
#pragma gBuffer DefaultGCutout MeshMain PixelgBuffer CUTOUT

// MSVert/MSPrim, the per-thread meshlet/instance state, and the fetch/decode/output helpers
// (LoadMeshlet, DecodeVertex*, BuildOutputVertex, DecodeMeshletTriangle) live in
// meshCommon.hlsli, shared with terrainmesh.hlsl (Milestone 1 terrain, plan step 6).

[RootSignature(SeeDRootSignature)]
[outputtopology("triangle")]
[numthreads(HLSL::max_triangles, 1, 1)]
void MeshMain(in uint3 groupId : SV_GroupID, in uint3 groupThreadId : SV_GroupThreadID, out vertices MSVert outVerts[HLSL::max_vertices], out indices uint3 outIndices[HLSL::max_triangles], out primitives MSPrim outPrims[HLSL::max_triangles])
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
        outPrims[groupThreadId.x].instanceIndex = drawInstanceIndex;
        outPrims[groupThreadId.x].meshletIndex = drawMeshletIndex;
    }
}

// Force early depth test so occluded fragments are rejected before the shader runs.
// Without this, the overdraw UAV write below makes D3D12 default to late depth testing,
// which shades every covered fragment regardless of draw order (defeating the front-to-back sort).
// Cutout variant (CUTOUT define) omits early-Z so the alpha-test discard runs BEFORE depth is
// written -- otherwise the transparent texels would still occlude what's behind them.
#ifndef CUTOUT
[earlydepthstencil]
#endif
PS_OUTPUT PixelgBuffer(MSVert inVerts, MSPrim inPrim)
{
    return GBufferPixel(inVerts, inPrim);
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