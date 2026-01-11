#include "structs.hlsl"

cbuffer CustomRT : register(b3)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

cbuffer CustomAS : register(b4)
{
    HLSL::AtmosphericScatteringParameters asParameters;
};
#define CUSTOM_ROOT_BUFFER_2

#include "binding.hlsl"
#include "common.hlsl"

#pragma raytracing 

GlobalRootSignature SeeDRootSignatureRT =
{
    SeeDRootSignature
};

struct SmallHitInfo
{
    bool hit;
};

[shader("raygeneration")]
void RayGen()
{
    uint seed = initRand(DispatchRaysIndex().xy * DispatchRaysIndex().z + DispatchRaysIndex().z);
    
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[asParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[asParameters.currentFroxelIndex];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    
    float zRand = nextRand(seed)  - 0.5;
    //zRand*=0.5;
    float3 froxelPos = DispatchRaysIndex().xyz;
    float3 prevFroxelPos = float3(DispatchRaysIndex().xyz) - (0.0,0.0,1.0);
    froxelPos.z += zRand;
    prevFroxelPos.z += zRand;
    float3 froxelWorldPos = FroxelToWorld(froxelPos, currentFroxel.resolution.xyz, asParameters.specialNear, camera);
    float3 prevFroxelWorldPos = DispatchRaysIndex().z > 0 ? FroxelToWorld(prevFroxelPos, currentFroxel.resolution.xyz, asParameters.specialNear, camera) : camera.worldPos.xyz;
    float prevFroxelDist = length(froxelWorldPos - prevFroxelWorldPos);
    
    RESTIRRay restirRay;
    restirRay.Origin = froxelWorldPos;
    restirRay = DirectLight(rtParameters, restirRay, 4, seed);
    restirRay.HitRadiance *= asParameters.luminosity;
    restirRay.HitRadiance = max(0.125, restirRay.HitRadiance);
    
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    froxelData[DispatchRaysIndex().xyz] = float4(restirRay.HitRadiance, asParameters.density * prevFroxelDist);
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    //CommonMiss(WorldRayOrigin(), WorldRayDirection(), RayTCurrent(), payload);
    payload.hitDistance = RayTCurrent();
    //payload.hitPos = WorldRayOrigin + WorldRayDirection * RayTCurrent;
    payload.color = 1;
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    //CommonHit(rtParameters, InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.bary, WorldRayOrigin(),  WorldRayDirection(), RayTCurrent(), 254/*ReportHit()*/, payload)
    payload.hitDistance = RayTCurrent();
    //payload.hitPos = WorldRayOrigin + WorldRayDirection * RayTCurrent;
    payload.color = 0.0;
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
    //payload.hitPos = WorldRayOrigin + WorldRayDirection * RayTCurrent;
    payload.color = 0.0;
}