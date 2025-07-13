#include "structs.hlsl"

cbuffer CustomRT : register(b3)
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
    float3 currentColor = current[dtid.xy].xyz;
    float3 historyColor = history.SampleLevel(samplerLinear, cd.previousUV, 0);
    
    // Apply clamping on the history color.
    float3 NearColor0 = current[dtid.xy + int2(1, 0)].xyz;
    float3 NearColor1 = current[dtid.xy + int2(0, 1)].xyz;
    float3 NearColor2 = current[dtid.xy + int2(-1, 0)].xyz;
    float3 NearColor3 = current[dtid.xy + int2(0, -1)].xyz;
    
    float3 BoxMin = min(currentColor, min(NearColor0, min(NearColor1, min(NearColor2, NearColor3))));
    float3 BoxMax = max(currentColor, max(NearColor0, max(NearColor1, max(NearColor2, NearColor3))));;
    
    historyColor = clamp(historyColor, BoxMin, BoxMax);
    
    float modulationFactor = 0.9;
    
    /*
    float foundMatch = 0;
    for (int x = -1; x<2; x++)
    {
        for (int y = -1; y<2; y++)
        {
            int2 neighbourPixel = dtid.xy + int2(x, y);
            GBufferCameraData neighbourcd = GetGBufferCameraData(neighbourPixel.xy);
            float distDiff = cd.viewDist - neighbourcd.previousViewDist;
            if((abs(distDiff) < (0.01 * cd.viewDist))) foundMatch = 1;
        }
    }
    
    if(foundMatch == 0) modulationFactor = 0;
    if (viewContext.frameNumber == 0) modulationFactor = 0;
    //if(cd.viewDist > 5000) modulationFactor = 0;
    //if(cd.viewDistDiff > 0.05) modulationFactor = 0;
    */
    
    float3 color = max(0, lerp(currentColor, historyColor, modulationFactor));
    
    current[dtid.xy].xyz = color;
}
