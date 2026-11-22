#pragma once

// Shared mesh-shader plumbing for mesh.hlsl and terrainmesh.hlsl (Milestone 1 terrain, plan step 6):
// meshlet/instance fetch, SNORM16 vertex decode, and clip-space output setup. Amplification is
// unused in this engine (mesh + pixel stages only), so there's nothing to factor there.
//
// Callers must #include "structs.hlsl", "binding.hlsl" and "common.hlsl" first (for
// ResourceDescriptorHeap-bound heap indices, i_octahedral_32, samplers, viewContext, etc.), then
// early-out groups with MeshDrawIndex(groupId) >= meshDrawCount, then call LoadMeshlet(drawIndex)
// from every thread before using any of the decode/output helpers below or SetMeshOutputCounts.

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
// instanceIndex/meshletIndex vary per group now that one DispatchMesh covers a whole shader
// bucket, so the pixel shader gets them per primitive instead of from root constants.
struct MSPrim
{
    uint primitiveID : SV_PrimitiveID; // triangle index within the meshlet
    nointerpolation uint instanceIndex : INSTANCE_INDEX;
    nointerpolation uint meshletIndex : MESHLET_INDEX;
};

// Per-thread copies of the group's draw state. Every thread loads it itself (the addresses are
// group-uniform, so the loads are broadcast) instead of thread 0 filling groupshared behind a group
// barrier: the per-vertex fetches no longer wait for that barrier, and only the fields actually used
// get loaded (a groupshared struct copy pulled in all of HLSL::Mesh, LOD table included).
static uint drawInstanceIndex;
static uint drawMeshletIndex;
static HLSL::Mesh mesh;
static HLSL::Meshlet meshlet;
static float4x4 worldMatrix;     // object->world (for normal/tangent)
static float4x4 mvp;             // viewProj * world
static float4x4 previousMvp;     // previousViewProj * previousWorld
static float worldDetSign;       // sign(det(world)) -> keeps binormal handedness correct on mirrored instances

// Flattened index of this group within the bucket's 2D dispatch grid (see HLSL::meshDispatchWidth).
// Group-uniform; groups at or past meshDrawCount (tail of the last row) must output nothing.
uint MeshDrawIndex(uint3 groupId)
{
    return groupId.y * HLSL::meshDispatchWidth + groupId.x;
}

// Every thread: fetches the instance/mesh/meshlet for this group into the per-thread state above.
void LoadMeshlet(uint drawIndex)
{
    bool sorted = (meshDrawBase & HLSL::meshDrawSortedBit) != 0;
    StructuredBuffer<HLSL::MeshletDraw> draws = ResourceDescriptorHeap[sorted ? viewContext.meshletsCulledArgsSortedSRVIndex : viewContext.meshletsCulledArgsSRVIndex];
    HLSL::MeshletDraw draw = draws[(meshDrawBase & ~HLSL::meshDrawSortedBit) + drawIndex];
    drawInstanceIndex = draw.instanceIndex;
    drawMeshletIndex = draw.meshletIndex;

    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[0]; //viewContext.cameraIndex];

    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[draw.instanceIndex];
    worldMatrix = instance.unpack(instance.current);
    float4x4 previousWorldMatrix = instance.unpack(instance.previous);

    // Concatenate once so each vertex does a single matrix*vector (no intermediate worldPos).
    mvp = mul(camera.viewProj, worldMatrix);
    previousMvp = mul(camera.previousViewProj, previousWorldMatrix);
    worldDetSign = determinant((float3x3)worldMatrix) >= 0.0f ? 1.0f : -1.0f;

    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    mesh = meshes[instance.meshIndex]; // for SNORM16 position decode (aabbMin / aabbExtent)

    StructuredBuffer<HLSL::Meshlet> meshlets = ResourceDescriptorHeap[commonResourcesIndices.meshletsHeapIndex];
    meshlet = meshlets[draw.meshletIndex];
    meshlet.vertexCount = min(HLSL::max_vertices, meshlet.vertexCount);
    meshlet.triangleCount = min(HLSL::max_triangles, meshlet.triangleCount);
}

// Vertex decode against this group's mesh (common.hlsl holds the shared decoders, also used by the
// visibility buffer resolve).
float3 DecodeVertexPositionOS(HLSL::Vertex v) { return DecodeMeshVertexPosition(mesh, v); }

float3 DecodeVertexNormalOS(HLSL::Vertex v) { return i_octahedral_32(v.normalOct, 16); }
float3 DecodeVertexTangentOS(HLSL::Vertex v) { return i_octahedral_32(v.tangentOct, 16); }
float DecodeVertexHandedness(HLSL::Vertex v) { return DecodeMeshVertexHandedness(v) * worldDetSign; }

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

    // Not normalized here: ResolveOpenPBR renormalizes after interpolation anyway.
    o.normal = mul((float3x3)worldMatrix, normalOS);
    o.tangent = float4(mul((float3x3)worldMatrix, tangentOS), handedness);
    o.uv = uv;
    return o;
}

// Decodes triangle indices for one meshlet triangle (groupThreadId < meshlet.triangleCount).
uint3 DecodeMeshletTriangle(uint groupThreadId)
{
    uint3 tri = DecodeMeshletTriangleIndices(meshlet.triangleOffset, groupThreadId);
    // a mirrored instance flips the screen-space winding: swap back so CULL_BACK keeps its front faces
    return worldDetSign < 0.0f ? tri.xzy : tri;
}

// Surface haze (structs.hlsl HazeMap / HazeLayer): splats this fragment's surface into its haze
// layer's map, called by the GBuffer pixel shaders after any alpha discard. One pixel per 4x4 tile per
// frame, in an order offset per tile so every pixel of the tile gets its turn over 16 frames, keeps the
// atomics cheap and evenly spread.
#define HAZE_SPLAT_EVERY_PIXEL 0 // debug: every GBuffer pixel splats, not one per 4x4 tile per frame
static const uint hazeBayer4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
void HazeSplat(float2 pixel, float4 currentPos, float3 geometricNormal, HLSL::Material material)
{
    uint layerIndex = material.hazeLayer;
    if (layerIndex == 0 || layerIndex >= HLSL::HazeLayerCount || viewContext.hazeMap.layersIndex == HLSL::invalidUINT)
        return;
#if !HAZE_SPLAT_EVERY_PIXEL
    uint2 p = uint2(pixel);
    uint2 tile = p >> 2;
    uint order = hazeBayer4x4[(p.y & 3) * 4 + (p.x & 3)] + JenkinsHash(tile.x ^ (tile.y << 16));
    if ((order & 15u) != (viewContext.frameNumber & 15u))
        return;
#endif

    StructuredBuffer<HLSL::HazeLayer> layers = ResourceDescriptorHeap[viewContext.hazeMap.layersIndex];
    HLSL::HazeLayer layer = layers[layerIndex];
    if (layer.splatIndex == HLSL::invalidUINT)
        return;

    // walls, trunks and the undersides of overhangs must not write heights (per layer: foliage lowers
    // it); a double-sided surface is seen from either side
    float up = normalize(geometricNormal).y;
    if ((material.flags & HLSL::MaterialFlagDoubleSided) != 0)
        up = abs(up);
    if (up < layer.upThreshold)
        return;

    // currentPos is the unjittered clip position, viewProj_inv the unjittered inverse
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    float4 world = mul(cameras[viewContext.cameraIndex].viewProj_inv, float4(currentPos.xyz / currentPos.w, 1.0f));
    world.xyz /= world.w;

    uint height = HazeQuantize(world.y);
    RWTexture2D<uint> splat = ResourceDescriptorHeap[layer.splatIndex];
    [unroll]
    for (uint level = 0; level < HLSL::HazeMapLevels; level++)
    {
        uint2 texel;
        if (HazeTexel(HazeCell(world.xz, HazeTexelSize(layer, level)), HazeOrigin(layer.origin, level), level, layer.resolution, texel))
            InterlockedMax(splat[texel], height);
    }
}

// ---- GBuffer pixel shader --------------------------------------------------------------------
// A visibility buffer: the pixel shaders only record which triangle covers the pixel, plus its motion.
// The material is resolved afterwards, once per pixel instead of once per overdrawn fragment, by
// gbufferResolve.hlsl (DLSS-RR guides) and by the path tracer's primary hit (common.hlsl
// GetVisibilityHit). The only material reads left here are the alpha test and the haze splat.
// PSO render target formats come from this struct's reflection (Loading.h): visibility is
// R32G32_UINT, motion R32G32_FLOAT (Renderer.h GBuffers): the targets must match these exactly.
struct PS_OUTPUT
{
    uint2 visibility : SV_Target0;
    float2 motion : SV_Target1;
};

PS_OUTPUT GBufferPixel(MSVert inVerts, MSPrim inPrim)
{
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instances[inPrim.instanceIndex].materialIndex];

#ifdef CUTOUT
    OpenPBRTexCoord uv = OpenPBRTexCoordGrad(inVerts.uv, ddx(inVerts.uv), ddy(inVerts.uv));
    if (OpenPBROpacity(material, uv) + 0.01f < material.alphaCutoff)
        discard;
#endif

    HazeSplat(inVerts.pos.xy, inVerts.currentPos, inVerts.normal, material);

    if (editorContext.debugFlags & HLSL::EditorDebugOverdraw) // count shaded fragments per pixel for the overdraw heatmap
    {
        RWTexture2D<uint> overdraw = ResourceDescriptorHeap[viewContext.overdrawIndex];
        uint previous;
        InterlockedAdd(overdraw[uint2(inVerts.pos.xy)], 1, previous);
    }

    PS_OUTPUT o;
    o.visibility = PackVisibility(inPrim.instanceIndex, inPrim.meshletIndex, inPrim.primitiveID);
    o.motion = CalcVelocity(inVerts.currentPos, inVerts.previousPos, viewContext.renderResolution.xy);
    return o;
}
