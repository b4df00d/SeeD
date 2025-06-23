#include "structs.hlsl"

cbuffer CustomRT : register(b2)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

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


#pragma raytracing 

GlobalRootSignature SeeDRootSignatureRT =
{
    SeeDRootSignature
};


[shader("raygeneration")]
void RayGen()
{
    //return;
    uint2 dtid = DispatchRaysIndex().xy;
    //if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y) return;
    
    GBufferCameraData cd = GetGBufferCameraData(dtid);
    if(cd.viewDist > 5000) return;
    
    SurfaceData s = GetSurfaceData(dtid.xy);
    
    uint seed = initRand(dtid.xy);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    HLSL::GIReservoir og = r;
    
    /*
    float3 bounceLightDir = normalize(r.hit_Wsum.xyz - cd.offsetedWorldPos);
    float3 bounceNorm;
    float3 bounceHit;
    float4 bounceLight = IndirectLight(rtParameters, s, bounceLightDir, cd.offsetedWorldPos, 0, seed, bounceNorm, bounceHit);
    float distDiff = saturate(abs(length(r.hit_Wsum.xyz - bounceHit)));
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    GI[dtid.xy] = smoothstep(0, 0.01, distDiff);
    return;
    */
    
    int pattern = (dtid.x + dtid.y + rtParameters.passNumber + viewContext.frameNumber) % 2;
    float2 radius = 8 * (2-rtParameters.passNumber) * lerp(nextRand(seed), 1, 0.1);
    uint spacialReuse = 0;
    for (uint i = 0; i < 4; i++)
    {
        float2 pixel = dtid.xy + (pattern == 0 ? patternA[i] : patternB[i]) * radius; // pourquoi avec un int2 pixel ca deconne un max ?
        if (pixel.x < 0 || pixel.y < 0) continue;
        if (pixel.x >= viewContext.renderResolution.x || pixel.y >= viewContext.renderResolution.y) continue;
        {
            GBufferCameraData cdNeightbor = GetGBufferCameraData(pixel.xy);
            
            if(abs(cd.viewDist - cdNeightbor.viewDist) > (0.2)) continue;
            if(dot(cd.worldNorm, cdNeightbor.worldNorm) < 0.9) continue;
            
            HLSL::GIReservoir rNeightbor = UnpackGIReservoir(giReservoir[pixel.x + pixel.y * viewContext.renderResolution.x]);
            
            //ScaleGIReservoir(rNeightbor, maxFrameFilteringCount/(1 + radius.x));
            MergeGIReservoir(r, rNeightbor, nextRand(seed));
            
            spacialReuse++;
        }
    }
    r = Validate(rtParameters, s, seed, cd.offsetedWorldPos, r, og, dtid);
    //r = og;
    //r.color_W.xyz = float3(1, 1, 0);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> previousgiReservoir = ResourceDescriptorHeap[rtParameters.previousgiReservoirIndex];
    previousgiReservoir[dtid.x + dtid.y * viewContext.renderResolution.x] = PackGIReservoir(r);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    float3 gi = GI[dtid.xy];
    gi = spacialReuse / 4.0f;
    //gi = s.albedo.xyz;
    //GI[dtid.xy] = gi;
    
    uint2 debugPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
    if(abs(length(debugPixel - dtid)) < 3)
    {
        //DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + r.dir_Wcount.xyz);
    }
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    CommonMiss(WorldRayOrigin(), WorldRayDirection(), RayTCurrent(), payload);
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    CommonHit(rtParameters, InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.bary, WorldRayOrigin(),  WorldRayDirection(), RayTCurrent(), 254/*ReportHit()*/, payload);
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
}