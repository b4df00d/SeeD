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

SurfaceData GetRTSurfaceData(HLSL::Attributes attrib)
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
    if (material.textures[0] != ~0)
    {
        Texture2D<float4> albedo = ResourceDescriptorHeap[material.textures[0]];
        s.albedo = albedo.SampleLevel(samplerLinear, uv, 0).xyz;
    }
    else
    {
        s.albedo = 0.66;
    }
    if (material.textures[2] != ~0)
    {
        Texture2D<float4> roughness = ResourceDescriptorHeap[material.textures[2]];
        s.roughness = roughness.SampleLevel(samplerLinear, uv, 0).x;
    }
    else
    {
        s.roughness = 0.99;
    }
    s.metalness = 0.1;

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
    hitNorm = normalize(hitNorm);
    // Get 2 random numbers to select our sample with
    float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

    // Cosine weighted hemisphere sample from RNG
    float3 bitangent = getPerpendicularVector(hitNorm);
    float3 tangent = cross(bitangent, hitNorm);
    float r = sqrt(randVal.x);
    float phi = 2.0f * 3.14159265f * randVal.y;

    // Get our cosine-weighted hemisphere lobe sample direction
    return normalize(tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x));
}


[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    StructuredBuffer<HLSL:: Light > lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    
    GBufferCameraData cd = GetGBufferCameraData(launchIndex);
    
    float3 offsetedWorldPos = cd.worldPos - (cd.viewDir * cd.viewDist * 0.01) + (cd.worldNorm * cd.viewDist * 0.0001);
    
    uint seed = initRand(launchIndex.x + cullingContext.frameTime % 1024, launchIndex.y + cullingContext.frameTime % 1024, 3);
    
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
    payload.hitDistance = RayTCurrent();
    if (payload.rayDepth >= HLSL::maxRTDepth) return;
    
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    
    SurfaceData s = GetRTSurfaceData(attrib);
    
    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * (RayTCurrent() * 0.999f) + s.normal * 0.001f;
    
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
    float3 sun = 0;
    if (payload.rayDepth < HLSL::maxRTDepth)
    {
        // shadow
        HLSL::HitInfo shadowload;
        shadowload.color = float3(0.0, 0.0, 0.0);
        shadowload.rayDepth = 1000000;
        shadowload.rndseed = payload.rndseed;
    
        RayDesc ray;
        ray.Origin = hitLocation;
        ray.Direction = -light.dir.xyz;
        ray.TMin = 0;
        ray.TMax = 100000;
        
        if (dot(-s.normal, ray.Direction) > 0)
        {
            //TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, shadowload);
            TraceRay(BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, ray, shadowload);
        }
        
        sun = shadowload.hitDistance >= ray.TMax ? light.color.xyz : 0;
    }
    payload.color = BRDF(s, WorldRayDirection(), -light.dir.xyz, sun);
    payload.color += BRDF(s, WorldRayDirection(), bounceLightDir, bounceLight);
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
    //payload.color = 0;
}