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
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    RWTexture2D<float4> albedo = ResourceDescriptorHeap[rtParameters.albedoIndex];
    
    lighted[dtid.xy] = float4(albedo[dtid.xy].xyz * GI[dtid.xy].xyz, 1);
}
