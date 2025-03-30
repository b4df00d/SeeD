#include "structs.hlsl"

cbuffer CustomRT : register(b2)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Lighting

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void Lighting(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    Texture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    Texture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    Texture2D<float4> albedo = ResourceDescriptorHeap[cullingContext.albedoIndex];
    
    GBufferCameraData cd = GetGBufferCameraData(dtid.xy);
    
    float3 indirect = GI[dtid.xy].xyz;
    float3 direct = shadows[dtid.xy] * sunColor;
    
    SurfaceData s;
    s.albedo = albedo[dtid.xy].xyz;
    s.roughness = 0.99;
    s.normal = cd.worldNorm;
    s.metalness = 0.1;
    
    float3 brdf = BRDF(s, cd.viewDir, sunDir, direct);
    float3 ambient = indirect * s.albedo;
    
    float3 result = brdf + ambient;
    
    lighted[dtid.xy] = float4(result, 1);
}
