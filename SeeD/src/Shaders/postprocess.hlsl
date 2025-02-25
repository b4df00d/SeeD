#include "structs.hlsl"

cbuffer CustomRT : register(b2)
{
    HLSL::PostProcessParameters ppParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute PostProcess

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void PostProcess(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if (dtid.x > ppParameters.resolution.x || dtid.y > ppParameters.resolution.y)
        return;
    
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[ppParameters.lightedIndex];
    RWTexture2D<float4> albedo = ResourceDescriptorHeap[ppParameters.albedoIndex];
    
    albedo[dtid.xy] = float4(lighted[dtid.xy].xyz, 1); // write back in the albedo becasue it has the same format as backbuffer and we'll copy it just after this compute shader
}
