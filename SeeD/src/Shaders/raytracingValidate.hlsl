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
    uint2 launchIndex = DispatchRaysIndex().xy;
    
    if (launchIndex.x > viewContext.renderResolution.x || launchIndex.y > viewContext.renderResolution.y)
        return;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    GBufferCameraData cd = GetGBufferCameraData(launchIndex);
    
    if(cd.viewDist > 5000) return;
    
    SurfaceData s = GetSurfaceData(launchIndex.xy);
    
    uint seed = initRand(launchIndex.xy);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[launchIndex.x + launchIndex.y * viewContext.renderResolution.x]);
    HLSL::GIReservoir og = r;
    
    r = Validate(rtParameters, s, seed, cd.offsetedWorldPos, r, og, launchIndex);
    
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