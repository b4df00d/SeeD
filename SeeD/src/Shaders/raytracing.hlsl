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

[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    StructuredBuffer<HLSL:: Light > lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    
    GBufferCameraData cd = GetGBufferCameraData(launchIndex);
    
    float3 offsetedWorldPos = cd.worldPos - (cd.viewDir * cd.viewDist * 0.01) + (cd.worldNorm * cd.viewDist * 0.0001);
    
    uint seed = initRand(launchIndex.x + cullingContext.frameTime % 234 * 1.621f, launchIndex.y + cullingContext.frameTime % 431 * 1.432f, 4);
    
    float shadow = 0;
    {
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
        
        bounceLight += payload.color * saturate(dot(cd.worldNorm, bounceLightDir));
    }
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    GI[launchIndex] = bounceLight;
    RWTexture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    shadows[launchIndex] = shadow;

}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    payload.hitDistance = RayTCurrent();
    payload.color = float3(0.66, 0.75, 0.99) * pow(dot(WorldRayDirection(), float3(0, 1, 0)) * 0.5 + 0.5, 2) * 0.3;
    //payload.color = 0;
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
    //light.dir.xyz = -light.dir.xyz;
    
    SurfaceData s = GetRTSurfaceData(attrib);
    
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
        if (dot(-s.normal, -ray.Direction) > 0)
        {
            //TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, shadowload);
            TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, ray, shadowload);
        }
        
        sun = shadowload.hitDistance >= ray.TMax ? light.color.xyz : 0;
    }
    //payload.color = BRDF(s, WorldRayDirection(), -light.dir.xyz, sun) * 10;
    payload.color = dot(s.normal, -light.dir.xyz) * sun * s.albedo;
    
#if false
    float3 bounceLight = 0;
    float3 bounceLightDir = 0;
    float hitDistance = 100000;
    if (payload.rayDepth < HLSL::maxRTDepth-1)
    {
        bounceLightDir = lerp(s.normal, getCosHemisphereSample(payload.rndseed, s.normal), 0.9);
        
        HLSL::HitInfo nextRay;
        nextRay.color = float3(0.0, 0.0, 0.0);
        nextRay.rayDepth = payload.rayDepth + 1;
        nextRay.rndseed = payload.rndseed;
    
        RayDesc ray;
        ray.Origin = hitLocation;
        ray.Direction = bounceLightDir;
        ray.TMin = 0;
        ray.TMax = hitDistance;
        
        TraceRay(BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, nextRay);
        
        bounceLight = nextRay.color;
    }
    //payload.color += BRDF(s, WorldRayDirection(), bounceLightDir, bounceLight);
    payload.color += bounceLight * s.albedo;
#else
    float3 bounceLightDir = lerp(s.normal, getCosHemisphereSample(payload.rndseed, s.normal), 0.9);
    
    float3 samplePos = hitLocation;
    float3 cellSize = float3(rtParameters.probesBBMax.xyz - rtParameters.probesBBMin.xyz) / float3(rtParameters.probesResolution.xyz);
    int3 launchIndex = (samplePos - rtParameters.probesBBMin.xyz) / (rtParameters.probesBBMax.xyz - rtParameters.probesBBMin.xyz) * rtParameters.probesResolution.xyz;
    uint3 wrapIndex = ModulusI(launchIndex.xyz + rtParameters.probesAddressOffset.xyz, rtParameters.probesResolution.xyz);
    uint probeIndex = wrapIndex.x + wrapIndex.y * rtParameters.probesResolution.x + wrapIndex.z * (rtParameters.probesResolution.x * rtParameters.probesResolution.y);
    StructuredBuffer<HLSL::SHProbe> probes = ResourceDescriptorHeap[rtParameters.probesIndex];
    HLSL::SHProbe probe = probes[probeIndex];
    float3 bounceLight = max(0.0f, shUnproject(probe.R, probe.G, probe.B, s.normal)); // A "max" is usually recomended to avoid negative values (can happen with SH)
    
    //payload.color += BRDF(s, WorldRayDirection(), bounceLightDir, bounceLight);
    payload.color += bounceLight * s.albedo;
#endif
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
    //payload.color = 0;
}