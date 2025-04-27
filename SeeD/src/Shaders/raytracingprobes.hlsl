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
    
    HLSL::ProbeGrid probes = rtParameters.probes[rtParameters.probeToCompute];
    
    float3 t = float3(launchIndex.xyz) / float3(probes.probesResolution.xyz);
    float3 probeWorldPos = lerp(probes.probesBBMin.xyz, probes.probesBBMax.xyz, t);
    float3 cellSize = float3(probes.probesBBMax.xyz - probes.probesBBMin.xyz) / float3(probes.probesResolution.xyz);
    probeWorldPos += cellSize * 0.5;
    
    uint3 wrapIndex = ModulusI(launchIndex.xyz + probes.probesAddressOffset.xyz, probes.probesResolution.xyz);
    uint probeIndex = wrapIndex.x + wrapIndex.y * probes.probesResolution.x + wrapIndex.z * (probes.probesResolution.x * probes.probesResolution.y);
  
    RWStructuredBuffer<HLSL::ProbeData> probesBuffer = ResourceDescriptorHeap[probes.probesIndex];
    
    uint seed = initRand(launchIndex.x + cullingContext.frameTime % 234 * 1.621f, launchIndex.y + cullingContext.frameTime % 431 * 1.432f, 4);
    
    
    HLSL::ProbeData probeData = probesBuffer[probeIndex];
    HLSL::SHProbe probe = probeData.sh;
    
    //Initialise sh to 0
    //probe.R = shZero();
    //probe.G = shZero();
    //probe.B = shZero();
    
    // Accumulate coefficients according to surounding direction/color tuples.
    for (float az = 0.5f; az < probes.probesSamplesPerFrame; az += 1.0f)
    {
        for (float ze = 0.5f; ze < probes.probesSamplesPerFrame; ze += 1.0f)
    	{
            float3 rayDir = shGetUniformSphereSample(az / probes.probesSamplesPerFrame, ze / probes.probesSamplesPerFrame);
            
            HLSL::HitInfo payload;
            payload.color = float3(0.0, 0.0, 0.0);
            payload.rayDepth = 1;
            payload.rndseed = seed;
    
            RayDesc ray;
            float3 randVal = (float3(nextRand(seed), nextRand(seed), nextRand(seed)) * 2.f - 1.f) * cellSize * 0.125f;
            ray.Origin = probeWorldPos + probeData.position.xyz;//  + randVal;
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
    // and take in consideration the previous samples too ? (if we do not reset to zero above)
    float shFactor = 4.0 * shPI / ((probes.probesSamplesPerFrame * probes.probesSamplesPerFrame) * 1.44);
    probe.R = shScale(probe.R, shFactor);
    probe.G = shScale(probe.G, shFactor);
    probe.B = shScale(probe.B, shFactor);
    
    probeData.sh = probe;
    probeData.position = 100000;
    probesBuffer[probeIndex] = probeData;
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    //payload.color = float3(0, 0, 1); return;
    payload.hitDistance = RayTCurrent();
    payload.color = float3(0.66, 0.75, 0.99) * pow(dot(WorldRayDirection(), float3(0, 1, 0)) * 0.5 + 0.5, 2) * 2;
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
    payload.color = saturate(dot(s.normal, -light.dir.xyz)) * sun;
    payload.color *= saturate(s.albedo * 1);
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
    //payload.color = 0;
}