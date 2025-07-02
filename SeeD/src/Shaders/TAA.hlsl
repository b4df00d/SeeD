#include "structs.hlsl"

cbuffer CustomRT : register(b2)
{
    HLSL::TAAParameters TAAParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute TAA

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void TAA(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    if (dtid.x > viewContext.displayResolution.x || dtid.y > viewContext.displayResolution.y)
        return;
    
    GBufferCameraData cd = GetGBufferCameraData(dtid.xy);
    
    RWTexture2D<float3> current = ResourceDescriptorHeap[TAAParameters.lightedIndex];
    Texture2D<float3> history = ResourceDescriptorHeap[TAAParameters.historyIndex];
    
    float modulationFactor = 0.9;
    if (viewContext.frameNumber == 0)
        modulationFactor = 0;
    
    float3 currentColor = current[dtid.xy].xyz;
    float3 historyColor = history.SampleLevel(samplerLinear, cd.previousUV, 0);
    float3 color = max(0, lerp(currentColor, historyColor, modulationFactor));
    
    //history[dtid.xy].xyz = color;
    current[dtid.xy].xyz = color;
}
