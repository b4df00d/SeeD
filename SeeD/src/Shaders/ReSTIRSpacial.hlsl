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
    if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y)
        return;
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];   
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
 
    Texture2D<float3> normalT = ResourceDescriptorHeap[viewContext.normalIndex];
    Texture2D<float> depthT = ResourceDescriptorHeap[viewContext.depthIndex];
    
    float3 worldNorm = ReadR11G11B10Normal(normalT[dtid.xy]);
    float depth = depthT[dtid.xy];
    
    uint seed = initRand(gtid.x + viewContext.frameTime % 234 * 1.621f, gtid.y + viewContext.frameTime % 431 * 1.432f, 4);
    
    uint pattern = (dtid.x + dtid.y + rtParameters.passNumber + viewContext.frameNumber) % 2;
    float radius = 6 * (rtParameters.passNumber+1) * lerp(nextRand(seed), 1, 0.125);
    uint spacialReuse = 0;
    for (uint i = 0; i < 4; i++)
    {
        int2 pixel = dtid.xy + (pattern == 0 ? patternA[i] : patternB[i]) * radius;
        if (pixel.x < 0 || pixel.y < 0) continue;
        if (pixel.x >= viewContext.renderResolution.x || pixel.y >= viewContext.renderResolution.y) continue;
        {
            float3 worldNormNeightbor = ReadR11G11B10Normal(normalT[pixel.xy]);
            float depthNeightbor = depthT[pixel.xy];
            if(abs(depth - depthNeightbor) > 0.002)
                continue;
            if(dot(worldNorm, worldNormNeightbor) < 0.5)
                continue;
            
            HLSL::GIReservoir rNeightbor = UnpackGIReservoir(giReservoir[pixel.x + pixel.y * viewContext.renderResolution.x]);
            UpdateGIReservoir(r, rNeightbor);
            spacialReuse++;
        }
    }
    
    ScaleGIReservoir(r, maxFrameFilteringCount, 0.99);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> previousgiReservoir = ResourceDescriptorHeap[rtParameters.previousgiReservoirIndex];
    previousgiReservoir[dtid.x + dtid.y * viewContext.renderResolution.x] = PackGIReservoir(r);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    //GI[dtid.xy] = float3(GI[dtid.xy].x, GI[dtid.xy].y, spacialReuse / 4.0f);
}
