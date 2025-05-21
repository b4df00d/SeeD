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
    
    GBufferCameraData cd = GetGBufferCameraData(dtid.xy);
    
    if(cd.viewDist > 5000) return;
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];   
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    
    uint seed = initRand(dtid.xy);
    
    uint pattern = (dtid.x + dtid.y + rtParameters.passNumber + viewContext.frameNumber) % 2;
    float radius = 64 * (2-rtParameters.passNumber) * lerp(nextRand(seed), 1, 0.015);
    uint spacialReuse = 0;
    for (uint i = 0; i < 4; i++)
    {
        int2 pixel = dtid.xy + (pattern == 0 ? patternA[i] : patternB[i]) * radius;
        if (pixel.x < 0 || pixel.y < 0) continue;
        if (pixel.x >= viewContext.renderResolution.x || pixel.y >= viewContext.renderResolution.y) continue;
        {
            GBufferCameraData cdNeightbor = GetGBufferCameraData(pixel.xy);
            if(abs(cd.viewDist - cdNeightbor.viewDist) > (0.5)) continue;
            if(dot(cd.worldNorm, cdNeightbor.worldNorm) < 0.8) continue;
            
            HLSL::GIReservoir rNeightbor = UnpackGIReservoir(giReservoir[pixel.x + pixel.y * viewContext.renderResolution.x]);
            rNeightbor.color_W.w *= 1.0/(radius * 100);
            UpdateGIReservoir(r, rNeightbor);
            spacialReuse++;
        }
    }
    
    ScaleGIReservoir(r, maxFrameFilteringCount, 1);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> previousgiReservoir = ResourceDescriptorHeap[rtParameters.previousgiReservoirIndex];
    previousgiReservoir[dtid.x + dtid.y * viewContext.renderResolution.x] = PackGIReservoir(r);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    float3 gi = GI[dtid.xy];
    gi = spacialReuse / 4.0f;
    //GI[dtid.xy] = gi;
}
