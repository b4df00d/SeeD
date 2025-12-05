#define RT_SHADER

#include "structs.hlsl"

cbuffer CustomRT : register(b3)
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
    
    HLSL::ProbeData probeData = probesBuffer[probeIndex];
    
    // check if the probe is still active
    if (probeData.position_Activation.w <= 0) return;
    
    HLSL::SHProbe probe = probeData.sh;
    
    //Initialise sh to 0
    probe.R = shZero();
    probe.G = shZero();
    probe.B = shZero();
    
    uint seed = initRand(launchIndex.xy);
    // Accumulate coefficients according to surounding direction/color tuples.
    for (float az = 0.5f; az < probes.probesSamplesPerFrame; az += 1.0f)
    {
        for (float ze = 0.5f; ze < probes.probesSamplesPerFrame; ze += 1.0f)
    	{
            float3 rayDir = shGetUniformSphereSample(az / probes.probesSamplesPerFrame, ze / probes.probesSamplesPerFrame);
            
            RayDesc ray;
            float3 randVal = (float3(nextRand(seed), nextRand(seed), nextRand(seed)) * 2.f - 1.f) * cellSize * 0.125f;
            ray.Origin = probeWorldPos + probeData.position_Activation.xyz; //  + randVal;
            ray.Direction = rayDir;
            ray.TMin = 0;
            ray.TMax = 100000;
    
            HLSL::HitInfo payload;
            payload.color = float3(0.0, 0.0, 0.0);
            payload.type = 1; //indirect
            payload.depth = 0;
            payload.seed = seed;
        
            TraceRayCommon(rtParameters, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
            
            float3 color = payload.color;
    
    		sh2 sh = shEvaluate(rayDir);
            probe.R = shAdd(probe.R, shScale(sh, color.r));
    		probe.G = shAdd(probe.G, shScale(sh, color.g));
    		probe.B = shAdd(probe.B, shScale(sh, color.b));
    	}
    }
    
    // integrating over a sphere so each sample has a weight of 4*PI/samplecount (uniform solid angle, for each sample)
    // and take in consideration the previous samples too ? (if we do not reset to zero above)
    float shFactor = 4.0 * shPI / (probes.probesSamplesPerFrame * probes.probesSamplesPerFrame);
    probe.R = shScale(probe.R, shFactor);
    probe.G = shScale(probe.G, shFactor);
    probe.B = shScale(probe.B, shFactor);
    
    probeData.sh = probe;
    probeData.position_Activation.xyz = 0;
    probeData.position_Activation.w = max(0, probeData.position_Activation.w-1);
    probesBuffer[probeIndex] = probeData;
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