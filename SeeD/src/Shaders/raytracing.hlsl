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

void DebugSurfaceData(float3 pos, float3 dir)
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    HLSL::HitInfo payload;
    payload.color = float3(0.0, 0.0, 0.0);
    payload.rayDepth = 1;
    payload.rndseed = 0;
    
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0;
    ray.TMax = 100000;
    
    TraceRay( BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    GI[launchIndex] = payload.color;
}

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
    uint2 launchIndex = DispatchRaysIndex().xy;
    
    if (launchIndex.x > viewContext.renderResolution.x || launchIndex.y > viewContext.renderResolution.y)
        return;
    
    GBufferCameraData cd = GetGBufferCameraData(launchIndex); 
    if(cd.viewDist > 5000) return;
    
    uint seed = initRand(launchIndex.xy);
    
    float shadow = 0;
    {
        StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
        HLSL::Light light = lights[0];
        
        HLSL::HitInfo shadowload;
        shadowload.color = float3(0.0, 0.0, 0.0);
        shadowload.hitPos = 0;
        shadowload.hitDistance = 0;
        shadowload.rayDepth = 11111;
        shadowload.rndseed = seed;
    
        RayDesc ray;
        ray.Origin = cd.offsetedWorldPos;
        ray.Direction = -light.dir.xyz;
        ray.TMin = 0;
        ray.TMax = 100000;

        if (dot(cd.worldNorm, ray.Direction) > 0)
        {
            TraceRayCommon(rtParameters, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, ray, shadowload);
        }
        
        shadow = shadowload.hitDistance >= ray.TMax ? 1 : 0;
    }
    
    float3 bounceLight = 0;
    float3 bounceLightDir = 0;
    {
        HLSL::HitInfo payload;
        payload.color = float3(0.0, 0.0, 0.0);
        payload.hitPos = 0;
        payload.hitDistance = 0;
        payload.rayDepth = 1;
        payload.rndseed = seed;
    
        bounceLightDir = normalize(lerp(cd.worldNorm, getCosHemisphereSample(payload.rndseed, cd.worldNorm), 0.9));
        RayDesc ray;
        ray.Origin = cd.offsetedWorldPos;
        ray.Direction = bounceLightDir;
        ray.TMin = 0;
        ray.TMax = 100000;
        
        TraceRayCommon(rtParameters, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        
        bounceLight = payload.color;// * saturate(dot(cd.worldNorm, bounceLightDir)); // BRDF here ?
        
        // ReSTIR
        RWStructuredBuffer<HLSL::GIReservoirCompressed> previousgiReservoir = ResourceDescriptorHeap[rtParameters.previousgiReservoirIndex];
        HLSL::GIReservoir r = UnpackGIReservoir(previousgiReservoir[cd.previousPixel.x + cd.previousPixel.y * viewContext.renderResolution.x]);
        
        #ifdef JITTER
        for(uint i = 0; i < 4; i++)
        {
            uint2 jitterPixel = cd.previousPixel + patternA[i];
            
            if (jitterPixel.x < 0 || jitterPixel.y < 0) continue;
            if (jitterPixel.x >= viewContext.renderResolution.x || jitterPixel.y >= viewContext.renderResolution.y) continue;
            
            HLSL::GIReservoir r2 = UnpackGIReservoir(previousgiReservoir[jitterPixel.x + jitterPixel.y * viewContext.renderResolution.x]);
            //ScaleGIReservoir(r2, maxFrameFilteringCount * 2, 0.75);
            UpdateGIReservoir(r, r2);
        }
        ScaleGIReservoir(r, maxFrameFilteringCount, 1); 
        #endif
        
        float blend = 1-saturate(cd.viewDistDiff * pow(cd.viewDist, 0.5) * 0.5 - 0.2);
        uint frameFilteringCount = max(1, blend * maxFrameFilteringCount);
        //blend = min(0.999, blend);
    
        HLSL::GIReservoir newR;
        float W = length(bounceLight);
        newR.color_W = float4(bounceLight, W);
        newR.dir_Wcount = float4(bounceLightDir, 1);
        newR.hit_Wsum = float4(payload.hitPos, W);
        
        // if not first time fill with previous frame reservoir
        if (viewContext.frameNumber == 0)
        {
            r.color_W = 0;
            r.dir_Wcount = 0;
            r.hit_Wsum = 0;
        }
        
        UpdateGIReservoir(r, newR);
        ScaleGIReservoir(r, frameFilteringCount, blend);
        
        RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
        giReservoir[launchIndex.x + launchIndex.y * viewContext.renderResolution.x] = PackGIReservoir(r);
        
        // end ReSTIR
        
        RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
        GI[launchIndex] = float3(1-blend, 1 - saturate(r.dir_Wcount.w / float(maxFrameFilteringCount)), 0);
        bounceLight = r.color_W.xyz * (r.hit_Wsum.w / r.dir_Wcount.w);
        //GI[launchIndex] = bounceLight;
        //GI[launchIndex] = r.color_W.xyz;
    }
    
    RWTexture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    shadows[launchIndex] = shadow;
    
    //DebugSurfaceData(cd.camera.worldPos.xyz, cd.viewDir); 
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    MyMissColorCalculation(WorldRayOrigin(), WorldRayDirection(), RayTCurrent(), payload);
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    if (payload.rayDepth >= HLSL::maxRTDepth) return;
    ShadeMyTriangleHit(rtParameters, InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.bary, WorldRayOrigin(),  WorldRayDirection(), RayTCurrent(), 254/*ReportHit()*/, payload);
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
}