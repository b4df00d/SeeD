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

#define TRACING_DISTANCE                1000.0f
#define BOUNCES_MIN                     3
#define RIS_CANDIDATES_LIGHTS           8 // Number of candidates used for resampling of analytical lights
#define SHADOW_RAY_IN_RIS               0 // Enable this to cast shadow rays for each candidate during resampling. This is expensive but increases quality
#define ENABLE_SPECULAR_LOBE            1 // Enable to use the specular lobe for splitting between diffuse and specular BRDFs

#include "raytracingCommon.hlsl"

#pragma raytracing Update

GlobalRootSignature SeeDRootSignatureRT =
{
    SeeDRootSignature
};

[shader("raygeneration")]
void RayGen()
{
    uint seed = initRand(DispatchRaysIndex().xy * DispatchRaysIndex().z + DispatchRaysIndex().z);
    const uint2 launchIndex = DispatchRaysIndex().xy;
    const uint2 launchDimensions = DispatchRaysDimensions().xy;
    
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[asParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[asParameters.currentFroxelIndex];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    RaytracingAccelerationStructure AccelerationStructure = ResourceDescriptorHeap[rtParameters.BVH];
    
    float zRand = nextRand(seed)  - 0.5;
    //zRand*=0.5;
    float3 froxelPos = DispatchRaysIndex().xyz;
    float3 prevFroxelPos = float3(DispatchRaysIndex().xyz) - (0.0,0.0,1.0);
    froxelPos.z += zRand;
    prevFroxelPos.z += zRand;
    float3 froxelWorldPos = FroxelToWorld(froxelPos, currentFroxel.resolution.xyz, asParameters.specialNear, camera);
    float3 prevFroxelWorldPos = DispatchRaysIndex().z > 0 ? FroxelToWorld(prevFroxelPos, currentFroxel.resolution.xyz, asParameters.specialNear, camera) : camera.worldPos.xyz;
    float prevFroxelDist = length(froxelWorldPos - prevFroxelWorldPos);
    
    float3 sampleRadiance = 0;
    
    // Evaluate direct light (next event estimation), start by sampling one light 
    HLSL::Light light;
    float lightWeight;
    
    uint rngState = InitRNG(launchIndex, launchDimensions, viewContext.frameTime);
    if (SampleLightRIS(rngState, froxelWorldPos, 0, light, lightWeight, AccelerationStructure))
    {
        // Prepare data needed to evaluate the light
        float3 incidentVector;
        float lightDistance;
        float irradiance;
        float2 rand2 = float2(RNG(rngState), RNG(rngState));
        GetLightData(light, froxelWorldPos, rand2, rtParameters.enableSoftShadows, incidentVector, lightDistance, irradiance);
        float3 vectorToLight = -incidentVector;

        // Cast shadow ray towards the selected light
        float3 lightVisibility = CastShadowRay(AccelerationStructure, froxelWorldPos, 0, vectorToLight, lightDistance);
        if (any(lightVisibility > 0.0f))
        {
            // If light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
            // This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
            float3 lightContribution = light.color.xyz * irradiance * lightWeight * lightVisibility * asParameters.luminosity;
            sampleRadiance += lightContribution;
        }
    }
    
    float density = min(exp(-froxelWorldPos.y * 0.02), 1) * asParameters.density * prevFroxelDist * PerlinNoise3_Normalized(froxelWorldPos * 0.005);
    
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    froxelData[DispatchRaysIndex().xyz] = float4(sampleRadiance, density);
}


[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
    payload.hitDistance = -1.0f;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.instanceID = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.geometryIndex = GeometryIndex();
    payload.barycentrics = attrib.uv;

    uint packedDistance = asuint(RayTCurrent()) & (~0x1u);
    packedDistance |= HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE ? 0x1 : 0x0;
    payload.hitDistance = asfloat(packedDistance);
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    SurfaceData s = GetRTSurfaceData(InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.uv);

    switch (s.shadingDomain)
    {
        case ShadingDomain::AlphaTested:
        case ShadingDomain::TransmissiveAlphaTested:
        {
            if (s.albedo.a < 0.5)
                IgnoreHit();

            break;
        }

        default:
            break;
    }
    // AcceptHit but continue looking for the closest hit
}

// TODO: Delete this
[shader("miss")]
void MissShadow(inout ShadowRayPayload payload : SV_RayPayload)
{
}

[shader("closesthit")]
void ClosestHitShadow(inout ShadowRayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.visibility = float3(0.0f, 0.0f, 0.0f);
}

[shader("anyhit")]
void AnyHitShadow(inout ShadowRayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    SurfaceData s = GetRTSurfaceData(InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.uv);

    switch (s.shadingDomain)
    {
        case ShadingDomain::AlphaTested:
        case ShadingDomain::TransmissiveAlphaTested:
        {
            if (s.albedo.a < 0.5)
                IgnoreHit();

            break;
        }

        default:
            // Modulate the visiblity by the material's transmission
            payload.visibility *= (1.0f - s.albedo.a) * s.albedo.rgb;
            if (dot(payload.visibility, 0.333f) > 0.001f)
                IgnoreHit();

            break;
    }
    AcceptHitAndEndSearch();
}

#ifndef RAY_DISPATCH
#pragma compute Accumulation AtmosphericScatteringAccumulation

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void AtmosphericScatteringAccumulation(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint seed = initRand(dtid.xy * dtid.z);
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[asParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[asParameters.currentFroxelIndex];
    HLSL::Froxels historyFroxel = froxels[asParameters.historyFroxelIndex];
    
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    RWTexture3D<float4> historyFroxelData = ResourceDescriptorHeap[historyFroxel.index];
    
    float4 accumulatedRadiance = float4(0, 0, 0, 0);
    for (uint z = 0; z < currentFroxel.resolution.z; z++)
    {
        uint3 fro = uint3(dtid.xy, z);
        float4 froData = froxelData[fro];
        historyFroxelData[fro] = froData;
        accumulatedRadiance.xyz += exp(-accumulatedRadiance.w) * (froData.xyz * froData.w);
        accumulatedRadiance.w += froData.w;
        froxelData[fro] = accumulatedRadiance;
    }
}

#pragma compute Reprojection AtmosphericScatteringReprojection

[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 8)]
void AtmosphericScatteringReprojection(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint seed = initRand(dtid.xy * dtid.z);
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    StructuredBuffer<HLSL::Froxels> froxels = ResourceDescriptorHeap[asParameters.froxelsIndex];
    HLSL::Froxels currentFroxel = froxels[asParameters.currentFroxelIndex];
    HLSL::Froxels historyFroxel = froxels[asParameters.historyFroxelIndex];
    
    float3 currentWorldPos = FroxelToWorld(dtid.xyz, currentFroxel.resolution.xyz, asParameters.specialNear, camera);
    float3 historyFroxelPos = WorldTohistoryFroxelUVW(currentWorldPos, historyFroxel.resolution.xyz, asParameters.specialNear, camera);
    
    Texture3D<float4> historyFroxelData = ResourceDescriptorHeap[historyFroxel.index];
    float4 previousData = historyFroxelData.Sample(samplerLinearClamp, historyFroxelPos);
    
    RWTexture3D<float4> froxelData = ResourceDescriptorHeap[currentFroxel.index];
    if (viewContext.frameNumber > 10)
        froxelData[dtid.xyz] = lerp(previousData, froxelData[dtid.xyz], 0.2);
}
#endif
