#include "structs.hlsl"

cbuffer CustomBake : register(b3)
{
    HLSL::TerrainBakeParameters bake;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

// Terrain node vertex bake: displaces one node's copy of the shared grid mesh directly into its
// own vertex range (MeshStorage::CreateMeshOverride), so raster (terrainmesh.hlsl) and RT (via
// the override's own BLAS, rebuilt right after this dispatch -- see TerrainErosion::Render) both
// see the same baked geometry instead of the gbuffer path re-sampling the heightmap every frame.
// One dispatch per dirty node, one thread per vertex; mirrors terrainmesh.hlsl's MeshMainTerrain
// live-displacement math exactly, just writing into the vertices pool instead of a rasterized
// vertex. Own file (not terrainErosion.hlsl): every #pragma compute entry in one file shares ONE
// Custom1Register cbuffer type (see AtmosphericScattering.hlsl), and this needs a completely
// different shape than TerrainErosionParameters.

// HLSL counterpart to GPU.h's QuantizeSNorm16/OctEncode32 (CPU) and common.hlsl's
// i_octahedral_32 (decode) -- bit-for-bit match required so re-baked vertices decode identically
// everywhere else (raster + RT). Only this shader needs the encode side today; promote to
// common.hlsl if a second consumer (skinning) needs it.
int QuantizeSNorm16(float v)
{
    float c = clamp(v, -1.0, 1.0);
    return (int)round(c * 32767.0);
}

uint OctEncode32(float3 n)
{
    float l1 = abs(n.x) + abs(n.y) + abs(n.z);
    n = l1 > 1e-12 ? n / l1 : float3(0, 0, 1);
    if (n.z < 0.0)
    {
        float2 signs = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signs;
    }
    uint dx = (uint)floor((0.5 + 0.5 * n.x) * 65535.0 + 0.5);
    uint dy = (uint)floor((0.5 + 0.5 * n.y) * 65535.0 + 0.5);
    return (dy << 16) | dx;
}

#pragma compute Bake TerrainMeshBakeMain

[RootSignature(SeeDRootSignature)]
[numthreads(64, 1, 1)]
void TerrainMeshBakeMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= bake.vertexCount)
        return;

    RWStructuredBuffer<HLSL::Vertex> vertices = ResourceDescriptorHeap[bake.verticesUAVIndex];
    HLSL::Vertex sv = vertices[bake.sourceVertexOffset + dtid.x];

    // Decode the source (flat grid) position against the SOURCE's own AABB -- mirrors
    // meshCommon.hlsli's DecodeVertexPositionOS exactly.
    int3 qi = int3(int(sv.packedPos.x << 16) >> 16, int(sv.packedPos.x) >> 16, int(sv.packedPos.y << 16) >> 16);
    float3 q = max(float3(qi) / 32767.0, -1.0);
    float3 objectPos = bake.sourceAabbMin.xyz + (q * 0.5 + 0.5) * bake.sourceAabbExtent.xyz;

    // Source tangent + handedness (terrain nodes have no rotation/mirroring -> worldDetSign is
    // always +1, unlike meshCommon.hlsli's general DecodeVertexHandedness).
    float3 tangentOS = i_octahedral_32(sv.tangentOct, 16);
    float handedness = (int(sv.packedPos.y) >> 16) >= 0 ? 1.0 : -1.0;

    float3 worldPosXZ = bake.nodeWorldPosScaleXZ.xyz + objectPos * float3(bake.nodeWorldPosScaleXZ.w, 1.0, bake.nodeWorldPosScaleXZ.w);
    float2 uv = worldPosXZ.xz / max(bake.worldExtent, 1e-5) + 0.5;

    Texture2D<float4> heightmapTex = ResourceDescriptorHeap[bake.heightmapIndex];
    float texW, texH;
    heightmapTex.GetDimensions(texW, texH);
    float2 texel = float2(1.0 / max(texW, 1.0), 1.0 / max(texH, 1.0));

    float hC  = SampleHeightBlurred(heightmapTex, samplerLinearClamp, uv, texel, bake.heightmapBlurRadius);
    float hX1 = SampleHeightBlurred(heightmapTex, samplerLinearClamp, uv + float2(texel.x, 0), texel, bake.heightmapBlurRadius);
    float hX0 = SampleHeightBlurred(heightmapTex, samplerLinearClamp, uv - float2(texel.x, 0), texel, bake.heightmapBlurRadius);
    float hZ1 = SampleHeightBlurred(heightmapTex, samplerLinearClamp, uv + float2(0, texel.y), texel, bake.heightmapBlurRadius);
    float hZ0 = SampleHeightBlurred(heightmapTex, samplerLinearClamp, uv - float2(0, texel.y), texel, bake.heightmapBlurRadius);

    objectPos.y += hC * bake.heightScale + bake.heightOffset;

    // Node instances now carry an identity Transform.scale -- the node's world footprint is baked
    // directly into the vertex positions here instead (and into the override's own AABB, set to
    // match at CreateMeshOverride time -- see Terrain.h TryAttachOverride), so no scale is ever
    // applied again downstream. That also means the world-space gradient below (already
    // world-units-per-world-unit, via worldTexel) IS the correct object-space gradient in this new
    // scaled-object-space directly -- no inverse-transpose folding needed anymore.
    float2 worldTexel = max(bake.worldExtent * texel, float2(1e-5, 1e-5));
    float dHdx = ((hX1 - hX0) * bake.heightScale) / (2.0 * worldTexel.x);
    float dHdz = ((hZ1 - hZ0) * bake.heightScale) / (2.0 * worldTexel.y);
    float3 normalOS = normalize(float3(-dHdx, 1.0, -dHdz));
    tangentOS = normalize(tangentOS - normalOS * dot(tangentOS, normalOS));

    // Bake the node's XZ scale into the position itself (Y is already in world units via
    // heightScale) -- must happen after the UV/gradient work above, which needs the ORIGINAL
    // (unscaled, source-grid-local) objectPos.x/z.
    objectPos.x *= bake.nodeWorldPosScaleXZ.w;
    objectPos.z *= bake.nodeWorldPosScaleXZ.w;

    // Re-encode SNORM16 against the OVERRIDE's own AABB (its Y range is real, unlike the flat
    // source's degenerate one -- see MeshStorage::CreateMeshOverride).
    float3 invExtent = float3(
        bake.outputAabbExtent.x > 0.0 ? 1.0 / bake.outputAabbExtent.x : 0.0,
        bake.outputAabbExtent.y > 0.0 ? 1.0 / bake.outputAabbExtent.y : 0.0,
        bake.outputAabbExtent.z > 0.0 ? 1.0 / bake.outputAabbExtent.z : 0.0);
    int qx = QuantizeSNorm16((objectPos.x - bake.outputAabbMin.x) * invExtent.x * 2.0 - 1.0);
    int qy = QuantizeSNorm16((objectPos.y - bake.outputAabbMin.y) * invExtent.y * 2.0 - 1.0);
    int qz = QuantizeSNorm16((objectPos.z - bake.outputAabbMin.z) * invExtent.z * 2.0 - 1.0);
    int hSnorm = handedness >= 0.0 ? 32767 : -32767;

    HLSL::Vertex ov;
    ov.packedPos = uint2(
        (uint(qx) & 0xFFFFu) | ((uint(qy) & 0xFFFFu) << 16),
        (uint(qz) & 0xFFFFu) | ((uint(hSnorm) & 0xFFFFu) << 16));
    ov.uv = uv; // world-space UV, matching terrainmesh.hlsl's live path (v.uv = uv there too)
    ov.normalOct = OctEncode32(normalOS);
    ov.tangentOct = OctEncode32(tangentOS);

    vertices[bake.outputVertexOffset + dtid.x] = ov;
}
