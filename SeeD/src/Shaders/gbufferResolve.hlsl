#include "structs.hlsl"

cbuffer CustomGBufferResolve : register(b3)
{
    uint4 resolveTargets; // UAVs: x albedo (DLSS-RR diffuse albedo), y specular albedo, z normal (a = roughness)
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

// Material resolve of the visibility buffer, dispatched by GBuffers::Render right after the draws:
// once per pixel, resolves the full OpenPBR surface the pixel sees and writes what the screen-space
// passes need from it (GetGBufferCameraData reads the normal) and DLSS-RR's guides:
//   albedo          the directional albedo of the diffuse-like lobes (diffuse, fuzz) toward the camera
//   specularAlbedo  the same for the reflection lobes (coat, metal, specular, transmission)
//   normal          shading normal, a = specular roughness (DLSS-RR packed roughness mode)
// The path tracer's primary hit re-resolves the same surface itself (GetPrimarySurface): only the
// guides go through these targets.

#pragma compute Resolve GBufferResolve

// A/B switch for DLSS-RR's diffuse albedo guide: 1 = the base colour (base_weight * base_color, what
// the GBuffer held before the visibility buffer), 0 = the diffuse lobes' real directional albedo.
#define RR_DIFFUSE_GUIDE_BASE_COLOR 0

[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 1)]
void GBufferResolve(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= (uint2)viewContext.renderResolution.xy))
        return;

    RWTexture2D<float4> albedoOut = ResourceDescriptorHeap[resolveTargets.x];
    RWTexture2D<float4> specularAlbedoOut = ResourceDescriptorHeap[resolveTargets.y];
    RWTexture2D<float4> normalOut = ResourceDescriptorHeap[resolveTargets.z];

    VisibilityHit hit;
    if (!GetVisibilityHit(dtid.xy, hit))
    {
        albedoOut[dtid.xy] = 0.0f;
        specularAlbedoOut[dtid.xy] = 0.0f;
        normalOut[dtid.xy] = 0.0f;
        return;
    }

    OpenPBRSurface s = ResolveOpenPBR(hit.material, hit.uv, hit.normal, hit.tangent, hit.bitangent);

    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    float3 V = normalize(cameras[0].worldPos.xyz - hit.position);
    OpenPBRData c = OpenPBRPrepare(s, V);
    float3 diffuseAlbedo, specularAlbedo;
    OpenPBRGuides(c, diffuseAlbedo, specularAlbedo);

#if RR_DIFFUSE_GUIDE_BASE_COLOR
    diffuseAlbedo = s.base_weight * s.base_color;
#endif

    if (editorContext.debugFlags & HLSL::EditorDebugClusters)
        diffuseAlbedo = RandUINT(hit.meshletIndex); // per-meshlet debug color
    if (editorContext.debugFlags & HLSL::EditorDebugTriangles)
        diffuseAlbedo = RandUINT(hit.meshletIndex * HLSL::max_triangles + hit.triangleIndex); // unique seed per triangle

    albedoOut[dtid.xy] = float4(saturate(diffuseAlbedo), 1.0f);
    specularAlbedoOut[dtid.xy] = float4(saturate(specularAlbedo), 1.0f);
    normalOut[dtid.xy] = float4(StoreNormal(s.normal), s.specular_roughness);
}
