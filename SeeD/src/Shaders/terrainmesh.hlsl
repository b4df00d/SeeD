#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"
#include "meshCommon.hlsli"

// Milestone 1 terrain (plan step 6): copied from mesh.hlsl, sharing the meshlet fetch / vertex
// decode / output-setup helpers via meshCommon.hlsli. The only difference from mesh.hlsl is in
// MeshMainTerrain: after decoding the SNORM16 local position, it samples the bindless heightmap
// (heap index + heightScale/heightOffset ride on the material struct -- see World.h
// Components::Terrain*Slot/Param and structs.hlsl HLSL::Terrain* constants, kept in sync by
// World.h Systems::TerrainStreaming), displaces objectPos.y, and recomputes the normal from the
// heightmap's height-field gradient (finite difference of neighboring texels) instead of using the
// flat authored normal.

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
        HLSL::Vertex v = verticesData[index]; // single fetch

        float3 objectPos = DecodeVertexPositionOS(v);
        float3 tangentOS = DecodeVertexTangentOS(v);
        float handedness = DecodeVertexHandedness(v);
        float3 normalOS = float3(0, 1, 0); // flat-grid authored normal, overwritten by the gradient below

        StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
        HLSL::Instance instance = instances[instanceIndexIndirect];
        StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
        HLSL::Material material = materials[instance.materialIndex];

        uint heightmapIndex = material.textures[HLSL::TerrainHeightmapTextureSlot];
        float heightScale = material.parameters[HLSL::TerrainHeightScaleParam];
        float heightOffset = material.parameters[HLSL::TerrainHeightOffsetParam];
        float terrainWorldSize = material.parameters[HLSL::TerrainWorldSizeParam];
        float erodedIndexParam = material.parameters[HLSL::TerrainErodedHeightmapParam];// eroded heightmap index is a float param because a pass-created resource has no Handle<Texture>
        if (erodedIndexParam >= 0.0f)
            heightmapIndex = (uint)erodedIndexParam;

        if (heightmapIndex != HLSL::invalidUINT)
        {
            // Read as float4 (like the other bindless material textures, e.g. GetSurfaceData's
            // roughness/metalness fetches in common.hlsl) rather than a single-channel view, to
            // avoid depending on the imported heightmap's exact DXGI format; only .x is used.
            Texture2D<float4> heightmapTex = ResourceDescriptorHeap[heightmapIndex];

            // World-space UV instead of the mesh-local v.uv (0..1 across just THIS node's own
            // patch): every quadtree node, regardless of its own footprint/scale, must sample the
            // same heightmap texel for the same world XZ position, or neighboring nodes at
            // different LODs would sample different (stretched) parts of the heightmap and the
            // displacement wouldn't line up across node boundaries.
            float3 worldPosXZ = mul(worldMatrix, float4(objectPos, 1)).xyz;
            float2 uv = worldPosXZ.xz / max(terrainWorldSize, 1e-5f) + 0.5f;
            v.uv = uv;

            float texW, texH;
            heightmapTex.GetDimensions(texW, texH);
            float2 texel = float2(1.0f / max(texW, 1.0f), 1.0f / max(texH, 1.0f));

            float hC  = heightmapTex.SampleLevel(samplerLinearClamp, uv, 0).x;
            float hX1 = heightmapTex.SampleLevel(samplerLinearClamp, uv + float2(texel.x, 0), 0).x;
            float hX0 = heightmapTex.SampleLevel(samplerLinearClamp, uv - float2(texel.x, 0), 0).x;
            float hZ1 = heightmapTex.SampleLevel(samplerLinearClamp, uv + float2(0, texel.y), 0).x;
            float hZ0 = heightmapTex.SampleLevel(samplerLinearClamp, uv - float2(0, texel.y), 0).x;

            objectPos.y += hC * heightScale + heightOffset;

            float2 worldTexel = max(terrainWorldSize * texel, float2(1e-5f, 1e-5f));
            float scaleXZ = max(length(float3(worldMatrix[0].x, worldMatrix[1].x, worldMatrix[2].x)), 1e-5f);

            float dHdx = ((hX1 - hX0) * heightScale) / (2.0f * worldTexel.x);
            float dHdz = ((hZ1 - hZ0) * heightScale) / (2.0f * worldTexel.y);
            float3 normalObj = normalize(float3(-dHdx * scaleXZ, 1.0f, -dHdz * scaleXZ));
            tangentOS = normalize(tangentOS - normalObj * dot(tangentOS, normalObj));
            normalOS = float3(-dHdx / scaleXZ, 1.0f, -dHdz / scaleXZ);
        }

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
    o.albedo.xy = inVerts.uv.xy;
    
    uint heightmapIndex = material.textures[HLSL::TerrainHeightmapTextureSlot];
    float erodedIndexParam = material.parameters[HLSL::TerrainErodedHeightmapParam];
    if (erodedIndexParam >= 0.0f)
        heightmapIndex = (uint)erodedIndexParam;

    float diffIndexParam = material.parameters[HLSL::TerrainErosionDiffParam];
    if (diffIndexParam >= 0.0f)
    {
        Texture2D<float4> diffTex = ResourceDescriptorHeap[(uint)diffIndexParam];
        float diff = diffTex.SampleLevel(samplerLinearClamp, inVerts.uv.xy, 0).x;
        diff = smoothstep(0.5, 0.6, diff);
        o.albedo.xyz = lerp(float3(110, 122, 47) / 255.0f, float3(107, 99, 80) / 255.0, diff);
    }
    else if (heightmapIndex != HLSL::invalidUINT)
    {
        Texture2D<float4> heightmapTex = ResourceDescriptorHeap[heightmapIndex];
        o.albedo  = heightmapTex.SampleLevel(samplerLinearClamp, inVerts.uv.xy, 0).x;
        o.albedo.xyz = float3(110, 122, 47) / 255.0f;

    }
        
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
