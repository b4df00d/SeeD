#define RT_SHADER

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

[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    
    if (launchIndex.x > viewContext.renderResolution.x || launchIndex.y > viewContext.renderResolution.y)
        return;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    GBufferCameraData cd = GetGBufferCameraData(launchIndex);
    
    if(cd.viewDist > 5000) return;
    
    uint seed = initRand(launchIndex.xy);
    
    float3 offsetedWorldPos = cd.worldPos - (cd.viewDir * cd.viewDist * 0.005) + (cd.worldNorm * cd.viewDist * 0.002);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[launchIndex.x + launchIndex.y * viewContext.renderResolution.x]);
    
    float3 bounceLight = 0;
    float3 bounceLightDir = normalize(r.hit_Wsum.xyz - offsetedWorldPos);
        
    HLSL::HitInfo payload;
    payload.color = float3(0.0, 0.0, 0.0);
    payload.hitPos = 0;
    payload.hitDistance = 0;
    payload.rayDepth = 1;
    payload.rndseed = seed;
    
    RayDesc ray;
    ray.Origin = offsetedWorldPos;
    ray.Direction = bounceLightDir;
    ray.TMin = 0;
    ray.TMax = 100000;
        
    TraceRay(BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    bounceLight = payload.color;
     
    HLSL::GIReservoir validatedR;
    float W = length(bounceLight);
    validatedR.color_W = float4(bounceLight, W);
    //validatedR.dir_Wcount = float4(bounceLightDir, r.dir_Wcount.w);
    //validatedR.hit_Wsum = float4(payload.hitPos, r.hit_Wsum.w);
    validatedR.dir_Wcount = float4(bounceLightDir, 1);
    validatedR.hit_Wsum = float4(payload.hitPos, W);
    
    //compression of r.hit_Wsum.xy will result in some paths never being validated even is no spacial reuse and cam movement :?
    float distDiff = 1-saturate(abs(length(offsetedWorldPos-payload.hitPos) - length(offsetedWorldPos-r.hit_Wsum.xyz)) * 1000);
    
    //r.color_W.w *= 0;
    //UpdateGIReservoir(r, validatedR);
    
    float likeness = 1-saturate(pow(abs(W - r.color_W.w) / 3.0f, 0.9));
    likeness = lerp(0.5, 1, likeness);
    ScaleGIReservoir(r, maxFrameFilteringCount, likeness);
    
    giReservoir[launchIndex.x + launchIndex.y * viewContext.renderResolution.x] = PackGIReservoir(r);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    //GI[launchIndex] = float3(0, likeness, 0);
    //DebugSurfaceData(cd.camera.worldPos.xyz, cd.viewDir);
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    payload.hitDistance = RayTCurrent();
    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * 10000;
    payload.hitPos = hitLocation;
    payload.color = Sky(WorldRayDirection());
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * (RayTCurrent() * 0.999f);
    payload.hitPos = hitLocation;
    if (payload.rayDepth >= HLSL::maxRTDepth) return;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    
    SurfaceData s = GetRTSurfaceData(attrib);
    //payload.color = s.albedo.xyz; return;
    hitLocation += s.normal * 0.001f;
    
    if (dot(s.normal, WorldRayDirection()) > 0) s.normal = -s.normal; // if we touch the backface, invert the normal ?
    
    float3 sun = 0;
    if (payload.rayDepth < HLSL::maxRTDepth)
    {
        // shadow
        HLSL::HitInfo shadowload;
        shadowload.color = float3(0.0, 0.0, 0.0);
        shadowload.rayDepth = 1000000;
        shadowload.rndseed = payload.rndseed;
        shadowload.hitDistance = 10000;
    
        RayDesc ray;
        ray.Origin = hitLocation;
        ray.Direction = -light.dir.xyz;
        ray.TMin = 0;
        ray.TMax = shadowload.hitDistance;
        
        shadowload.hitDistance = 0;
        if (dot(s.normal, ray.Direction) > 0)
        {
            //TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, shadowload);
            TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, ray, shadowload);
        }
        
        sun = shadowload.hitDistance >= ray.TMax ? light.color.xyz : 0;
    }
    //payload.color = BRDF(s, WorldRayDirection(), -light.dir.xyz, sun) * 10;
    payload.color = saturate(dot(s.normal, -light.dir.xyz)) * sun;
    
    float3 bounceLight = SampleProbes(rtParameters, hitLocation, s.normal, true);
    payload.color += bounceLight;
    
    if (payload.rayDepth > 0)
        payload.color *= saturate(s.albedo.xyz * 1); // fake lighting equation
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
}