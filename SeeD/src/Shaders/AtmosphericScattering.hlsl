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

inline float Fade(float t)
{
    // 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float HashFloat3(in float3 p)
{
    // Simple hash -> [0,1)
    return frac(sin(dot(p, float3(127.1f, 311.7f,  74.7f))) * 43758.5453f);
}

inline float3 Grad3(in float3 p)
{
    // Generate a pseudo-random unit vector from integer corner position
    float h1 = HashFloat3(p + float3(0.0f, 0.0f, 0.0f));
    float h2 = HashFloat3(p + float3(12.9898f, 78.233f, 37.719f));
    float h3 = HashFloat3(p + float3(39.3467f, 11.135f, 83.155f));
    float3 r = float3(h1, h2, h3) * 2.0f - 1.0f;
    // Avoid costly normalize for tiny performance gain; normalizing gives slightly better isotropy
    return normalize(r);
}

float PerlinNoise3(in float3 p)
{
    // Integer lattice points
    float3 Pi = floor(p);
    float3 Pf = p - Pi; // fractional part

    // Compute fade curves for each component
    float3 f = float3(Fade(Pf.x), Fade(Pf.y), Fade(Pf.z));

    // Evaluate gradient dot distance for each of the 8 corners
    float3 c000 = Pi + float3(0.0f, 0.0f, 0.0f);
    float3 c100 = Pi + float3(1.0f, 0.0f, 0.0f);
    float3 c010 = Pi + float3(0.0f, 1.0f, 0.0f);
    float3 c110 = Pi + float3(1.0f, 1.0f, 0.0f);
    float3 c001 = Pi + float3(0.0f, 0.0f, 1.0f);
    float3 c101 = Pi + float3(1.0f, 0.0f, 1.0f);
    float3 c011 = Pi + float3(0.0f, 1.0f, 1.0f);
    float3 c111 = Pi + float3(1.0f, 1.0f, 1.0f);

    float n000 = dot(Grad3(c000), Pf - float3(0.0f, 0.0f, 0.0f));
    float n100 = dot(Grad3(c100), Pf - float3(1.0f, 0.0f, 0.0f));
    float n010 = dot(Grad3(c010), Pf - float3(0.0f, 1.0f, 0.0f));
    float n110 = dot(Grad3(c110), Pf - float3(1.0f, 1.0f, 0.0f));
    float n001 = dot(Grad3(c001), Pf - float3(0.0f, 0.0f, 1.0f));
    float n101 = dot(Grad3(c101), Pf - float3(1.0f, 0.0f, 1.0f));
    float n011 = dot(Grad3(c011), Pf - float3(0.0f, 1.0f, 1.0f));
    float n111 = dot(Grad3(c111), Pf - float3(1.0f, 1.0f, 1.0f));

    // Interpolate along X
    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);

    // Interpolate along Y
    float nxy0 = lerp(nx00, nx10, f.y);
    float nxy1 = lerp(nx01, nx11, f.y);

    // Interpolate along Z
    float nxyz = lerp(nxy0, nxy1, f.z);

    // Result is approximately in [-1,1]
    return nxyz;
}

float PerlinNoise3_Normalized(in float3 p)
{
    // Map [-1,1] -> [0,1]
    return PerlinNoise3(p) * 0.5f + 0.5f;
}

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
    
    float density = min(exp(-froxelWorldPos.y * 0.02), 1) * asParameters.density * prevFroxelDist * PerlinNoise3_Normalized(froxelWorldPos * 0.005);
    
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    froxelData[DispatchRaysIndex().xyz] = float4(restirRay.HitRadiance, density);
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