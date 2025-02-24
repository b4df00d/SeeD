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

/*
struct SurfaceData
{
    float3 albedo;
    float metalness;
    float3 normal;
    float roughness;
};
SurfaceData GetSurfaceData(Attributes attrib)
{
    DrawCall drawCallData = drawCall[InstanceID()];
    Material material = materials[globals.materialHeapIndex][drawCallData.materialIndex];

    uint iBase = PrimitiveIndex() * 3 + uint(drawCallData.boundingSphere.x);
    uint i1 = indices[drawCallData.meshIndex][iBase + 0] + uint(drawCallData.boundingSphere.y);
    uint i2 = indices[drawCallData.meshIndex][iBase + 1] + uint(drawCallData.boundingSphere.y);
    uint i3 = indices[drawCallData.meshIndex][iBase + 2] + uint(drawCallData.boundingSphere.y);

    float2 uv1 = meshesUV[drawCallData.entityID][i1].texCoord.xy;
    float2 uv2 = meshesUV[drawCallData.entityID][i2].texCoord.xy;
    float2 uv3 = meshesUV[drawCallData.entityID][i3].texCoord.xy;

    float2 uv = ((1 - attrib.bary.x - attrib.bary.y) * uv1 + attrib.bary.x * uv2 + attrib.bary.y * uv3);

    SurfaceData s;
    s.albedo = srv2Dfloat4[material.albedo].SampleLevel(mapsSampler, uv, 0).xyz;
    s.roughness = 1 - srv2Dfloat4[material.smoothness].SampleLevel(mapsSampler, uv, 0).xyz;
    //s.albedo = float3(uv1.xy, 0);

    float3 nrm1 = meshesOther[drawCallData.instanceCount][i1].normal.xyz;
    float3 nrm2 = meshesOther[drawCallData.instanceCount][i2].normal.xyz;
    float3 nrm3 = meshesOther[drawCallData.instanceCount][i3].normal.xyz;

    float3 nrm = ((1 - attrib.bary.x - attrib.bary.y) * nrm1 + attrib.bary.x * nrm2 + attrib.bary.y * nrm3);
    s.normal = normalize(nrm);
    s.normal = mul(s.normal, (float3x3) drawCallData.wMat);
    return s;
}
*/
// Utility function to get a vector perpendicular to an input vector 
//    (from "Efficient Construction of Perpendicular Vectors Without Branching")
float3 getPerpendicularVector(float3 u)
{
    float3 a = abs(u);
    uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
    uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
    uint zm = 1 ^ (xm | ym);
    return cross(u, float3(xm, ym, zm));
}
float3 getCosHemisphereSample(inout uint randSeed, float3 hitNorm)
{
    // Get 2 random numbers to select our sample with
    float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

    // Cosine weighted hemisphere sample from RNG
    float3 bitangent = getPerpendicularVector(hitNorm);
    float3 tangent = cross(bitangent, hitNorm);
    float r = sqrt(randVal.x);
    float phi = 2.0f * 3.14159265f * randVal.y;

    // Get our cosine-weighted hemisphere lobe sample direction
    return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x);
}


[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[0]; //cullingContext.cameraIndex];
    
    float3 clipSpace = float3(launchIndex * rtParameters.resolution.zw * 2 - 1, 1);
    float4 worldSpace = mul(camera.viewProj_inv, float4(clipSpace.x, -clipSpace.y, clipSpace.z, 1));
    worldSpace.xyz /= worldSpace.w;
    
    float3 rayDir = worldSpace.xyz - camera.worldPos.xyz;
    float rayLength = length(rayDir);
    rayDir /= rayLength;
    
    
    HLSL::HitInfo payload;
    payload.color = float3(0.0, 0.0, 0.0);
    payload.rayDepth = 0;
    payload.currentPosition = worldSpace.xyz;
    payload.rndseed = initRand(launchIndex.x * abs(worldSpace.y), launchIndex.y * abs(worldSpace.z), 3);;
    payload.normal = float3(0, 1, 0);
    payload.tCurrent = 0;
    
    
    // Define a ray, consisting of origin, direction, and the min-max distance values
    RayDesc ray;
    ray.Origin = camera.worldPos.xyz;
    ray.Direction = rayDir;
    ray.TMin = 0;
    ray.TMax = 100000;

    // Trace the ray
    TraceRay( BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    GI[launchIndex] = payload.color;
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    payload.color = float3(0.2, 0.3, 0.8);

}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.rayDepth++;
    if (payload.rayDepth <= 1)
    {
        float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * (RayTCurrent() - 0.0001);
    
        // Define a ray, consisting of origin, direction, and the min-max distance values
        RayDesc ray;
        ray.Origin = hitLocation;
        ray.Direction = getCosHemisphereSample(payload.rndseed, float3(0, 1, 0));
        ray.TMin = 0;
        ray.TMax = 100000;

        // Trace the ray
        RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
        TraceRay(BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    }
    
    if (payload.rayDepth == 1)
        payload.color = float3(0, 1, 0);
    if (payload.rayDepth == 2)
        payload.color = float3(0, 0, 1);
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.color = float3(1, 0, 0);
}