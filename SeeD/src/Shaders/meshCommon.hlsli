#pragma once

// Shared mesh-shader plumbing for mesh.hlsl and terrainmesh.hlsl (Milestone 1 terrain, plan step 6):
// meshlet/instance fetch, SNORM16 vertex decode, and clip-space output setup. Amplification is
// unused in this engine (mesh + pixel stages only), so there's nothing to factor there.
//
// Callers must #include "structs.hlsl", "binding.hlsl" and "common.hlsl" first (for
// ResourceDescriptorHeap-bound heap indices, i_octahedral_32, samplers, viewContext, etc.), then
// call LoadMeshletShared(groupThreadId) from thread 0 followed by GroupMemoryBarrierWithGroupSync()
// before using any of the decode/output helpers below or calling SetMeshOutputCounts.

struct MSVert
{
    float4 pos : SV_Position;
    float4 currentPos : TEXCOORD0;
    float4 previousPos : TEXCOORD1;
    float3 normal : NORMAL;
    float4 tangent : TEXCOORD2; // xyz = tangent, w = world-space handedness (binormal sign)
    float2 uv : TEXCOORD3;
};

// Mesh pipelines don't auto-generate SV_PrimitiveID for the pixel shader: the mesh shader
// must export it as a per-primitive attribute (used by the 'triangles' debug view).
struct MSPrim
{
    uint primitiveID : SV_PrimitiveID; // triangle index within the meshlet
};

groupshared HLSL::Mesh mesh;
groupshared HLSL::Meshlet meshlet;
groupshared float4x4 worldMatrix;     // object->world (for normal/tangent)
groupshared float4x4 mvp;             // viewProj * world
groupshared float4x4 previousMvp;     // previousViewProj * previousWorld
groupshared float worldDetSign;       // sign(det(world)) -> keeps binormal handedness correct on mirrored instances

// Thread 0 only: fetches the instance/mesh/meshlet for this group and populates the groupshared
// state above. Caller must GroupMemoryBarrierWithGroupSync() before using it / SetMeshOutputCounts.
void LoadMeshletShared(uint groupThreadId)
{
    if (groupThreadId != 0)
        return;

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

// Decode SNORM16 position (local to mesh AABB). packedPos.x = [x|y<<16], packedPos.y = [z|handedness<<16].
float3 DecodeVertexPositionOS(HLSL::Vertex v)
{
    int3 qi = int3(int(v.packedPos.x << 16) >> 16, int(v.packedPos.x) >> 16, int(v.packedPos.y << 16) >> 16);
    float3 q = max(float3(qi) / 32767.0f, -1.0f);
    return mesh.aabbMin.xyz + (q * 0.5f + 0.5f) * mesh.aabbExtent.xyz;
}

float3 DecodeVertexNormalOS(HLSL::Vertex v) { return i_octahedral_32(v.normalOct, 16); }
float3 DecodeVertexTangentOS(HLSL::Vertex v) { return i_octahedral_32(v.tangentOct, 16); }
float DecodeVertexHandedness(HLSL::Vertex v) { return ((int(v.packedPos.y) >> 16) >= 0 ? 1.0f : -1.0f) * worldDetSign; }

// objectPos/normalOS/tangentOS are object-space (pre-worldMatrix). Callers that displace the vertex
// (terrainmesh.hlsl sampling a heightmap) pass the already-displaced objectPos and a recomputed
// normalOS -- everything else (clip transform, jitter, motion vectors, world-space normal/tangent)
// is identical between mesh.hlsl and terrainmesh.hlsl.
MSVert BuildOutputVertex(float3 objectPos, float3 normalOS, float3 tangentOS, float handedness, float2 uv)
{
    MSVert o;
    float4 pos = float4(objectPos, 1);

    float4 clipPos = mul(mvp, pos);
    o.currentPos = clipPos;
    clipPos.xy += viewContext.jitter.xy * clipPos.w;
    o.pos = clipPos;

    o.previousPos = mul(previousMvp, pos);

    // Not normalized here: GetSurfaceData renormalizes after interpolation anyway.
    o.normal = mul((float3x3)worldMatrix, normalOS);
    o.tangent = float4(mul((float3x3)worldMatrix, tangentOS), handedness);
    o.uv = uv;
    return o;
}

// Decodes triangle indices for one meshlet triangle (groupThreadId < meshlet.triangleCount).
uint3 DecodeMeshletTriangle(uint groupThreadId)
{
    ByteAddressBuffer trianglesData = ResourceDescriptorHeap[commonResourcesIndices.meshletTrianglesHeapIndex]; // because of uint8 format
    uint offset = meshlet.triangleOffset + groupThreadId * 3;
    uint alignedOffset = offset & ~3;
    uint shift = (offset - alignedOffset) * 8;
    uint2 packedData = trianglesData.Load2(alignedOffset);

    // Funnel-shift the 3 index bytes down to bit 0 (HLSL shifts are mod-32: guard shift==0).
    uint tri3 = (packedData.x >> shift) | (shift == 0 ? 0 : packedData.y << (32 - shift));
    return uint3(tri3 & 0xff, (tri3 >> 8) & 0xff, (tri3 >> 16) & 0xff);
}
