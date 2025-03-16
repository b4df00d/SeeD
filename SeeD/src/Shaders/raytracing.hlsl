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

struct SurfaceData
{
    float3 albedo;
    float metalness;
    float3 normal;
    float roughness;
};
SurfaceData GetSurfaceData(HLSL::Attributes attrib)
{
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[InstanceID()];
    
    StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instance.materialIndex];
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    StructuredBuffer<HLSL::Vertex> verticesData = ResourceDescriptorHeap[commonResourcesIndices.verticesHeapIndex];
    
    StructuredBuffer<uint> indicesData = ResourceDescriptorHeap[commonResourcesIndices.indicesHeapIndex];

    uint iBase = PrimitiveIndex() * 3 + mesh.indexOffset;
    uint i1 = indicesData[iBase + 0];
    uint i2 = indicesData[iBase + 1];
    uint i3 = indicesData[iBase + 2];

    float2 uv1 = verticesData[i1].uv;
    float2 uv2 = verticesData[i2].uv;
    float2 uv3 = verticesData[i3].uv;

    float2 uv = ((1 - attrib.bary.x - attrib.bary.y) * uv1 + attrib.bary.x * uv2 + attrib.bary.y * uv3);

    SurfaceData s;
    Texture2D<float4> albedo = ResourceDescriptorHeap[material.textures[0]];
    //s.albedo = albedo.SampleLevel(samplerLinear, uv, 0).xyz;
    Texture2D<float4> roughness = ResourceDescriptorHeap[material.textures[2]];
    //s.roughness = roughness.SampleLevel(samplerLinear, uv, 0).x;
    s.albedo = 0.5;
    s.roughness = 0.5;

    float3 nrm1 = verticesData[i1].normal;
    float3 nrm2 = verticesData[i2].normal;
    float3 nrm3 = verticesData[i3].normal;

    float3 normal = ((1 - attrib.bary.x - attrib.bary.y) * nrm1 + attrib.bary.x * nrm2 + attrib.bary.y * nrm3);
    normal = normalize(normal);
    float4x4 worldMatrix = instance.unpack();
    float3 worldNormal = mul((float3x3) worldMatrix, normal);
    s.normal = normal;
    
    return s;
}
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
    
    Texture2D<float> depth = ResourceDescriptorHeap[rtParameters.depthIndex];
    Texture2D<float3> normalT = ResourceDescriptorHeap[rtParameters.normalIndex];
    float3 normal = normalize(normalT[launchIndex]);
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[0]; //cullingContext.cameraIndex];
    
    // inverse y depth depth[uint2(launchIndex.x, rtParameters.resolution.y - launchIndex.y)]
    float3 clipSpace = float3(launchIndex * rtParameters.resolution.zw * 2 - 1, depth[launchIndex]);
    float4 worldSpace = mul(camera.viewProj_inv, float4(clipSpace.x, -clipSpace.y, clipSpace.z, 1));
    worldSpace.xyz /= worldSpace.w;
    
    float3 rayDir = worldSpace.xyz - camera.worldPos.xyz;
    float rayLength = length(rayDir);
    rayDir /= rayLength;
    worldSpace.xyz = camera.worldPos.xyz + rayDir * (rayLength * 0.999f) + (normal * 0.01f);
    
    
    float shadow = 0;
    {
        // shadow
        HLSL::HitInfo shadowload;
        shadowload.color = float3(0.0, 0.0, 0.0);
        shadowload.rayDepth = 1000000;
        shadowload.currentPosition = worldSpace.xyz;
        shadowload.rndseed = initRand(uint(launchIndex.x * abs(worldSpace.z + normal.x) * 382) >> 1, uint(launchIndex.y * abs(worldSpace.x + normal.y) * 472) >> 1, 3);
        shadowload.normal = normal;
        shadowload.tCurrent = 0;
    
        RayDesc ray;
        ray.Origin = worldSpace.xyz;
        ray.Direction = float3(1, 1, 1);
        ray.TMin = 0;
        ray.TMax = 100000;

        if (dot(shadowload.normal, ray.Direction) > 0)
        {
            // Trace the ray
            TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, shadowload);
        }
        
        shadow += shadowload.color.b > 0.0 ? 1 : 0;
    }
    
    float3 light = 0;
    {
        // AO
        HLSL::HitInfo payload;
        payload.color = float3(0.0, 0.0, 0.0);
        payload.rayDepth = 1;
        payload.currentPosition = worldSpace.xyz;
        payload.rndseed = initRand(uint(launchIndex.x * abs(worldSpace.y + normal.z) * 582) >> 1, uint(launchIndex.y * abs(worldSpace.z + normal.x) * 672) >> 1, 3);
        payload.normal = normal;
        payload.tCurrent = 0;
    
        RayDesc ray;
        ray.Origin = worldSpace.xyz;
        ray.Direction = getCosHemisphereSample(payload.rndseed, normal);
        ray.TMin = 0;
        ray.TMax = 100000;

        // Trace the ray
        TraceRay( BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        
        light += payload.color;
    }
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    GI[launchIndex] = light;
    RWTexture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    shadows[launchIndex] = shadow;

}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    payload.color = float3(0.4, 0.5, 0.8) * pow(dot(WorldRayDirection(), float3(0, 1, 0)) * 0.75 + 0.25, 4);
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    //payload.rayDepth++;
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    SurfaceData s = GetSurfaceData(attrib);
    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * (RayTCurrent() - 0.000001);
    
    float3 sun = 0;
    if (payload.rayDepth < HLSL::maxRTDepth)
    {
        // shadow
        HLSL::HitInfo shadowload;
        shadowload.color = float3(0.0, 0.0, 0.0);
        shadowload.rayDepth = 1000000;
        shadowload.currentPosition = hitLocation;
        shadowload.rndseed = nextRand(payload.rndseed);
        shadowload.normal = float3(0, 1, 0);
        shadowload.tCurrent = 0;
    
        RayDesc ray;
        ray.Origin = hitLocation;
        ray.Direction = float3(1, 1, 1);
        ray.TMin = 0;
        ray.TMax = 100000;
        
        if (dot(payload.normal, ray.Direction) > 0)
        {
            // Trace the ray
            TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, shadowload);
        }
        
        sun += shadowload.color.b > 0.0 ? float3(1, 0.75, 0.65) * 3 : 0;
    }
    
    float3 light = 0;
    if (payload.rayDepth < HLSL::maxRTDepth)
    {
        
        HLSL::HitInfo nextRay;
        nextRay.color = float3(0.0, 0.0, 0.0);
        nextRay.rayDepth = payload.rayDepth + 1;
        nextRay.currentPosition = hitLocation;
        nextRay.rndseed = payload.rndseed;
        nextRay.normal = s.normal;
        nextRay.tCurrent = 0;
    
        // Define a ray, consisting of origin, direction, and the min-max distance values
        RayDesc ray;
        ray.Origin = hitLocation;
        ray.Direction = getCosHemisphereSample(nextRay.rndseed, s.normal);
        ray.TMin = 0;
        ray.TMax = 100000;

        // Trace the ray
        TraceRay(BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, nextRay);
        
        light = nextRay.color;
    }
    
    payload.color += s.albedo * (sun + light); // do PBR shit here
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.color = float3(0, 0, 0);
}