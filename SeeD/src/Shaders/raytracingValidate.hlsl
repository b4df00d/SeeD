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
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[launchIndex.x + launchIndex.y * viewContext.renderResolution.x]);
    HLSL::GIReservoir og = r;
    
    r = Validate(rtParameters, seed, cd.offsetedWorldPos, r, og);
    
    giReservoir[launchIndex.x + launchIndex.y * viewContext.renderResolution.x] = PackGIReservoir(r);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    //GI[launchIndex] = float3(fail, 0, 0);
    //GI[launchIndex] = bounceLight;
    //GI[launchIndex] = r.color_W.xyz;
    
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