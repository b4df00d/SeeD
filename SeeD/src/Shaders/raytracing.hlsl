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

/* 
// NEED TO USE THOSE !!

LocalRootSignature MyLocalRootSignature =
{
    "RootConstants( num32BitConstants = 4, b4 )"           // Cube constants        
};

TriangleHitGroup MyHitGroup =
{
    "", // AnyHit
    "ClosestHit", // ClosestHit
};

SubobjectToExportsAssociation MyLocalRootSignatureAssociation =
{
    "MyLocalRootSignature", // subobject name
    "MyHitGroup"             // export association 
};

RaytracingShaderConfig MyShaderConfig =
{
    16, // max payload size
    8 // max attribute size
};

RaytracingPipelineConfig MyPipelineConfig =
{
    1 // max trace recursion depth
};
*/

/*

    // Define a ray, consisting of origin, direction, and the min-max distance values
    RayDesc ray;
    ray.Origin = camera.worldPos.xyz;
    ray.Direction = rayDir;
    ray.TMin = 0;
    ray.TMax = 100000;

    // Trace the ray
    TraceRay(
        // Parameter name: AccelerationStructure 
        // Acceleration structure 
        BVH,
        // Parameter name: RayFlags 
        // Flags can be used to specify the behavior upon hitting a surface 
        RAY_FLAG_NONE,
        // Parameter name: InstanceInclusionMask 
        // Instance inclusion mask, which can be used to mask out some geometry to this ray by 
        // and-ing the mask with a geometry mask. The 0xFF flag then indicates no geometry will be 
        // masked 
        0xFF,
        // Parameter name: RayContributionToHitGroupIndex 
        // Depending on the type of ray, a given object can have several hit groups attached 
        // (ie. what to do when hitting to compute regular shading, and what to do when hitting 
        // to compute shadows). Those hit groups are specified sequentially in the SBT, so the value 
        // below indicates which offset (on 4 bits) to apply to the hit groups for this ray.
        0,
        // Parameter name: MultiplierForGeometryContributionToHitGroupIndex 
        // The offsets in the SBT can be computed from the object ID, its instance ID, but also simply 
        // by the order the objects have been pushed in the acceleration structure. This allows the 
        // application to group shaders in the SBT in the same order as they are added in the AS, in 
        // which case the value below represents the stride (4 bits representing the number of hit 
        // groups) between two consecutive objects. 
        0,
        // Parameter name: MissShaderIndex 
        // Index of the miss shader to use in case several consecutive miss shaders are present in the 
        // SBT. This allows to change the behavior of the program when no geometry have been hit, for 
        // example one to return a sky color for regular rendering, and another returning a full 
        // visibility value for shadow rays. This sample has only one miss shader, hence an index 0 
        0,
        // Parameter name: Ray 
        // Ray information to trace 
        ray,
        // Parameter name: Payload 
        // Payload associated to the ray, which will be used to communicate between the hit/miss 
        // shaders and the raygen 
        payload
    );
*/

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
    
    uint seed = initRand(launchIndex.x + viewContext.frameTime % 234 * 1.621f, launchIndex.y + viewContext.frameTime % 431 * 1.432f, 4);
    
    float3 offsetedWorldPos = cd.worldPos - (cd.viewDir * cd.viewDist * 0.005) + (cd.worldNorm * cd.viewDist * 0.002);
    
    float shadow = 0;
    {
        StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
        HLSL::Light light = lights[0];
        
        HLSL::HitInfo shadowload;
        shadowload.color = float3(0.0, 0.0, 0.0);
        shadowload.rayDepth = 11111;
        shadowload.rndseed = seed;
    
        RayDesc ray;
        ray.Origin = offsetedWorldPos;
        ray.Direction = -light.dir.xyz;
        ray.TMin = 0;
        ray.TMax = 100000;

        if (dot(cd.worldNorm, ray.Direction) > 0)
        {
            TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, ray, shadowload);
        }
        
        shadow = shadowload.hitDistance >= ray.TMax ? 1 : 0;
    }
    
    float3 bounceLight = 0;
    float3 bounceLightDir = 0;
    {
        HLSL::HitInfo payload;
        payload.color = float3(0.0, 0.0, 0.0);
        payload.rayDepth = 1;
        payload.rndseed = seed;
    
        bounceLightDir = lerp(cd.worldNorm, getCosHemisphereSample(payload.rndseed, cd.worldNorm), 0.9);
        RayDesc ray;
        ray.Origin = offsetedWorldPos;
        ray.Direction = bounceLightDir;
        ray.TMin = 0;
        ray.TMax = 100000;
        
        TraceRay( BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        
        bounceLight = payload.color;// * saturate(dot(cd.worldNorm, bounceLightDir)); // BRDF here ?
        
        // ReSTIR
        RWStructuredBuffer<HLSL::GIReservoirCompressed> previousgiReservoir = ResourceDescriptorHeap[rtParameters.previousgiReservoirIndex];
        HLSL::GIReservoir r = UnpackGIReservoir(previousgiReservoir[cd.previousPixel.x + cd.previousPixel.y * viewContext.renderResolution.x]);
        
        float blend = 1-saturate(max(cd.viewDistDiff-(0.01 * cd.viewDist), 0) * 10);
        uint frameFilteringCount = max(1, blend * maxFrameFilteringCount);
    
        HLSL::GIReservoir newR;
        float W = length(bounceLight);
        newR.color_W = float4(bounceLight, W);
        newR.dir_Wcount = float4(bounceLightDir, 1);
        newR.pos_Wsum = float4(offsetedWorldPos, W);
        
        // if not first time fill with previous frame reservoir
        if (viewContext.frameNumber == 0)
        {
            r.color_W = 0;
            r.dir_Wcount = 0;
            r.pos_Wsum = 0;
        }
        
        UpdateGIReservoir(r, newR);
        ScaleGIReservoir(r, frameFilteringCount, blend);
        
        RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
        giReservoir[launchIndex.x + launchIndex.y * viewContext.renderResolution.x] = PackGIReservoir(r);
        
        //bounceLight = r.color_W.xyz * (r.pos_Wsum.w / r.dir_Wcount.w);
        // end ReSTIR
        
        RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
        //GI[launchIndex] = float3(blend, r.dir_Wcount.w / maxFrameFilteringCount, 0);
        //GI[launchIndex] = bounceLight;
    }
    
    RWTexture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    shadows[launchIndex] = shadow;
    
    //DebugSurfaceData(cd.camera.worldPos.xyz, cd.viewDir);
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    payload.hitDistance = RayTCurrent();
    payload.color = float3(0.66, 0.75, 0.99) * saturate(pow(dot(WorldRayDirection(), float3(0, 1, 0)) * 0.5 + 0.5, 1.5));
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    //payload.color = 0; return;
    payload.hitDistance = RayTCurrent();
    if (payload.rayDepth >= HLSL::maxRTDepth) return;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    
    SurfaceData s = GetRTSurfaceData(attrib);
    //payload.color = s.albedo.xyz; return;
    if(dot(s.normal, WorldRayDirection()) > 0) s.normal = -s.normal; // if we touch the backface, invert the normal ?
    
    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * (RayTCurrent() * 0.999f) + s.normal * 0.001f;
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
    //payload.color = 0;
}