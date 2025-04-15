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


[shader("raygeneration")]
void RayGen()
{
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    uint3 launchIndex = DispatchRaysIndex().xyz;
    float3 t = float3(launchIndex.xyz) / float3(rtParameters.probesResolution.xyz);
    float3 probeWorldPos = lerp(rtParameters.probesBBMin.xyz, rtParameters.probesBBMax.xyz, t);
    
    uint3 wrapIndex = ModulusI(launchIndex.xyz + rtParameters.probesAddressOffset.xyz, rtParameters.probesResolution.xyz);
    uint probeIndex = wrapIndex.x + wrapIndex.y * rtParameters.probesResolution.x + wrapIndex.z * (rtParameters.probesResolution.x * rtParameters.probesResolution.y);
  
    RWStructuredBuffer<HLSL::SHProbe> probes = ResourceDescriptorHeap[rtParameters.probesIndex];
    //float3 rc = RandUINT(probeIndex);
    
    uint seed = initRand(launchIndex.x + cullingContext.frameTime % 234 * 1.621f, launchIndex.y + cullingContext.frameTime % 431 * 1.432f, 4);
    //uint seed = initRand(launchIndex.x, launchIndex.y, 4);
    
    // Initialise sh to 0
    HLSL::SHProbe probe;
    probe.R = shZero();
    probe.G = shZero();
    probe.B = shZero();
    
    // Accumulate coefficients according to surounding direction/color tuples.
    for (float az = 0.5f; az < rtParameters.probesSamplesPerFrame; az += 1.0f)
    {
        for (float ze = 0.5f; ze < rtParameters.probesSamplesPerFrame; ze += 1.0f)
    	{
            float3 rayDir = shGetUniformSphereSample(az / rtParameters.probesSamplesPerFrame, ze / rtParameters.probesSamplesPerFrame);
            
            HLSL::HitInfo payload;
            payload.color = float3(0.0, 0.0, 0.0);
            payload.rayDepth = 2;
            payload.rndseed = seed;
    
            RayDesc ray;
            float3 randVal = (float3(nextRand(seed), nextRand(seed), nextRand(seed)) * 2.f - 1.f) * 0.0f;
            ray.Origin = probeWorldPos + randVal;
            ray.Direction = rayDir;
            ray.TMin = 0;
            ray.TMax = 100000;
        
            TraceRay(BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
            
            float3 color = payload.color;
    
    		sh2 sh = shEvaluate(rayDir);
            probe.R = shAdd(probe.R, shScale(sh, color.r));
    		probe.G = shAdd(probe.G, shScale(sh, color.g));
    		probe.B = shAdd(probe.B, shScale(sh, color.b));
    	}
    }
    
    // integrating over a sphere so each sample has a weight of 4*PI/samplecount (uniform solid angle, for each sample)
    float shFactor = 4.0 * shPI / (rtParameters.probesSamplesPerFrame * rtParameters.probesSamplesPerFrame);
    probe.R = shScale(probe.R, shFactor);
    probe.G = shScale(probe.G, shFactor);
    probe.B = shScale(probe.B, shFactor);
    
    probes[probeIndex] = probe;
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    //payload.color = float3(0, 0, 1); return;
    payload.hitDistance = RayTCurrent();
    payload.color = float3(0.66, 0.75, 0.99) * pow(dot(WorldRayDirection(), float3(0, 1, 0)) * 0.5 + 0.5, 2) * 0.3;
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    //payload.color = float3(1, 0, 0); return;
    payload.hitDistance = RayTCurrent();
    if (payload.rayDepth >= HLSL::maxRTDepth) return;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    
    SurfaceData s = GetRTSurfaceData(attrib);
    
    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * (RayTCurrent() * 0.99f) + s.normal * 0.01f;
    
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
        //if (dot(s.normal, ray.Direction) > 0)
        {
            //TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, shadowload);
            TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, ray, shadowload);
        }
        
        sun = shadowload.hitDistance >= ray.TMax ? light.color.xyz : 0;
    }
    //payload.color = BRDF(s, WorldRayDirection(), -light.dir.xyz, sun);
    payload.color = saturate(dot(s.normal, -light.dir.xyz)) * sun * s.albedo;
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
    //payload.color = 0;
}