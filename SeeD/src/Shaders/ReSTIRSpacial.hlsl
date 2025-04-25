#include "structs.hlsl"

cbuffer CustomRT : register(b2)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute ReSTIRSpacial


static int2 patternA[4] =
{
    int2(-1, 0),
    int2(0, -1),
    int2(1, 0),
    int2(0, 1)
};
static int2 patternB[4] =
{
    int2(-1, -1),
    int2(1, -1),
    int2(1, 1),
    int2(-1, 1)
};

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void ReSTIRSpacial(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    RWStructuredBuffer<HLSL::GIReservoir> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];   
    HLSL::GIReservoir r = giReservoir[dtid.x + dtid.y * rtParameters.resolution.x];
 
    Texture2D<float3> normalT = ResourceDescriptorHeap[cullingContext.normalIndex];
    Texture2D<float> depthT = ResourceDescriptorHeap[cullingContext.depthIndex];
    
    float3 worldNorm = ReadR11G11B10Normal(normalT[dtid.xy]);
    float depth = depthT[dtid.xy];
    
    uint pattern = (dtid.x + dtid.y + rtParameters.passNumber) % 2;
    for (uint i = 0; i < 4; i++)
    {
        int2 pixel = dtid.xy + (pattern == 0 ? patternA[i] * 6 : patternB[i] * 4);
        if(!(any(pixel<0) || any(pixel>cullingContext.resolution.xy)))
        {
            float3 worldNormNeightbor = ReadR11G11B10Normal(normalT[pixel.xy]);
            float depthNeightbor = depthT[pixel.xy];
            if(abs(depth - depthNeightbor) > 0.001)
                continue;
            if(dot(worldNorm, worldNormNeightbor) < 0.9)
                continue;
            HLSL::GIReservoir rNeightbor = giReservoir[pixel.x + pixel.y * rtParameters.resolution.x];
            UpdateGIReservoir(r, rNeightbor);
            
        }
    }
    /*
    if (r.pos_Wcount.w >= maxFrameFilteringCount)
    {
        float factor = max(0, float(maxFrameFilteringCount) / max(r.pos_Wcount.w, 0.0f));
        r.color_W.w = max(0, r.color_W.w * factor);
        r.dir_Wsum.w *= factor;
        r.pos_Wcount.w = maxFrameFilteringCount;
    }
    */
    
    RWStructuredBuffer<HLSL::GIReservoir> previousgiReservoir = ResourceDescriptorHeap[rtParameters.previousgiReservoirIndex];
    //previousgiReservoir[dtid.x + dtid.y * rtParameters.resolution.x] = r;
}
