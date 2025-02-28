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
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    RWTexture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    RWTexture2D<float4> albedo = ResourceDescriptorHeap[rtParameters.albedoIndex];
    
    float3 indirect = GI[dtid.xy].xyz;
    float3 direct = shadows[dtid.xy];
    
    float3 result = albedo[dtid.xy].xyz * (direct + indirect);
    result *= 0.5;
    
    lighted[dtid.xy] = float4(result, 1);
}
