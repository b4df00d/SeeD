#include "structs.hlsl"

cbuffer CustomRT : register(b2)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma raytracing 

GlobalRootSignature SeeDRootSignatureRT =
{
    SeeDRootSignature
};

static int2 patternA[4] =
{
    int2(-1, 0),
    int2(1, 0),
    int2(0, -1),
    int2(0, 1)
};

[shader("raygeneration")]
void RayGen()
{
    uint2 dtid = DispatchRaysIndex().xy;
    //if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y) return;
    
    GBufferCameraData cd = GetGBufferCameraData(dtid); 
    if(cd.viewDist > 5000) return;
    
    SurfaceData s = GetSurfaceData(dtid.xy);
    
    uint seed = initRand(dtid.xy);
    
    float3 bounceDir;
    float3 bounceNorm;
    float3 bounceHit;
    float4 bounceLight = IndirectLightR(rtParameters, s, cd.offsetedWorldPos, 0, seed, bounceDir, bounceNorm, bounceHit);
    
    float shadow = DirectLight(rtParameters, s, cd.offsetedWorldPos, 0, seed).x > 0;
        
    RWTexture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    shadows[dtid] = shadow;
        
    // ReSTIR
    RWStructuredBuffer<HLSL::GIReservoirCompressed> previousgiReservoir = ResourceDescriptorHeap[rtParameters.previousgiReservoirIndex];
    HLSL::GIReservoir r = UnpackGIReservoir(previousgiReservoir[cd.previousPixel.x + cd.previousPixel.y * viewContext.renderResolution.x]);
    // if not first time fill with previous frame reservoir
    if (viewContext.frameNumber == 0)
    {
        r.color_W = 0;
        r.dir_Wcount = float4(0,0,0,1);
        r.hit_Wsum = 0;
    }
        
    float blend = 1-saturate(cd.viewDistDiff * pow(cd.viewDist, 0.5) * 0.15 - 0.1);
    uint frameFilteringCount = max(1, blend * maxFrameFilteringCount);
    
    HLSL::GIReservoir newR;
    float W = dot(bounceLight.xyz, float3(0.3, 0.59, 0.11));
    newR.color_W = float4(bounceLight.xyz, W);
    newR.dir_Wcount = float4(bounceDir, 1);
    newR.hit_Wsum = float4(bounceHit, W);
        
    UpdateGIReservoir(r, newR, nextRand(seed));
    ScaleGIReservoir(r, frameFilteringCount);
        
        
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x] = PackGIReservoir(r);
        
    // end ReSTIR
    
    HLSL::GIReservoir rd = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    uint2 debugPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
    if(abs(length(debugPixel - dtid)) < 10)
    {
        //DrawLine(cd.offsetedWorldPos, bounceHit);
        //DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + r.dir_Wcount.xyz);
        //DrawLine(cd.offsetedWorldPos, r.hit_Wsum.xyz);
    }
    /*
    if(dtid.x%10==0 && dtid.y%10==0)
    {
        if(length(cd.offsetedWorldPos - bounceHit) < 10)
            DrawLine(cd.offsetedWorldPos, bounceHit);
    }
    */
    
#define REFERENCE
#ifdef REFERENCE
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    float3 direct = ComputeLight(light, shadow, s, cd.viewDir);
    HLSL::Light indirectLight;
    indirectLight.pos = float4(0,0,0, 0);
    indirectLight.dir = float4(normalize(cd.worldPos - r.hit_Wsum.xyz), 0);
    indirectLight.color.xyz = bounceLight.xyz;
    indirectLight.range = 1;
    indirectLight.angle = 1;
    float3 indirect = ComputeLight(indirectLight, 1, s, cd.viewDir);
    float3 result = direct + indirect;
    //result *= 10;
        
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    if (viewContext.frameNumber == 0) GI[dtid] = result;
    GI[dtid] = (GI[dtid] * (r.dir_Wcount.w-1) + result) / r.dir_Wcount.w;
#endif
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