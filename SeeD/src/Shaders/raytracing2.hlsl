#include "structs.hlsl"

#define SHARC_ENABLE_64_BIT_ATOMICS 1
#define TRACING_DISTANCE                10000.0f
#define BOUNCES_MIN                     3
#define RIS_CANDIDATES_LIGHTS           8 // Number of candidates used for resampling of analytical lights
#define SHADOW_RAY_IN_RIS               0 // Enable this to cast shadow rays for each candidate during resampling. This is expensive but increases quality
#define ENABLE_SPECULAR_LOBE            1 // Enable to use the specular lobe for splitting between diffuse and specular BRDFs
#define SHARC_SEPARATE_EMISSIVE         1

#include "SharcCommon.h"

cbuffer CustomRT : register(b3)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

GlobalRootSignature SeeDRootSignatureRT =
{
    SeeDRootSignature
};


#include "raytracingCommon.hlsl"

#pragma raytracing Update SHARC_UPDATE
#pragma raytracing Query SHARC_QUERY


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

struct AccumulatedSampleData
{
    float3 radiance;
};

void UpdateSampleData(inout AccumulatedSampleData accumulatedSampleData, float3 sampleRadiance, bool isDiffusePath, float hitDistance)
{
    accumulatedSampleData.radiance += sampleRadiance;
}

float4 ResolveSampleData(inout AccumulatedSampleData accumulatedSampleData, uint sampleNum, float intensityScale)
{
    accumulatedSampleData.radiance *= (intensityScale / sampleNum);
    return float4(accumulatedSampleData.radiance, 1.0f);
    //u_Output[DispatchRaysIndex().xy] = float4(accumulatedSampleData.radiance, 1.0f);
}

void PathTraceRays()
{
#if (SHARC_UPDATE)
    const bool isUpdatePass = true;
#else // !(SHARC_UPDATE)
    const bool isUpdatePass = false;
#endif // !(SHARC_UPDATE)
    
    const uint2 launchIndex = DispatchRaysIndex().xy;
    const uint2 launchDimensions = DispatchRaysDimensions().xy;
    uint rngState = InitRNG(launchIndex, launchDimensions, viewContext.frameTime);

    AccumulatedSampleData accumulatedSampleData = (AccumulatedSampleData) 0;
    float3 debugColor = float3(0.0f, 0.0f, 0.0f);
    
    float2 rand2 = float2(RNG(rngState), RNG(rngState));
    uint2 pixel = (float2) launchIndex / (float2) launchDimensions * (float2) viewContext.renderResolution + (rand2 * rtParameters.probeDownsampling);
    GBufferCameraData cd = GetGBufferCameraData(pixel);
    RaytracingAccelerationStructure AccelerationStructure = ResourceDescriptorHeap[rtParameters.BVH];
    
    // Initialize SHARC parameters
    SharcParameters sharcParameters;
    {
        sharcParameters.gridParameters.cameraPosition = cd.camera.worldPos.xyz;
        sharcParameters.gridParameters.sceneScale = rtParameters.SHARCSceneScale;
        sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
        sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

        sharcParameters.hashMapData.capacity = rtParameters.SHARCEntriesNum;
        sharcParameters.hashMapData.hashEntriesBuffer = ResourceDescriptorHeap[rtParameters.SHARCHashEntriesBufferIndex];

#if !SHARC_ENABLE_64_BIT_ATOMICS
        sharcParameters.hashMapData.lockBuffer = u_SharcLockBuffer;
#endif // !SHARC_ENABLE_64_BIT_ATOMICS

        sharcParameters.accumulationBuffer = ResourceDescriptorHeap[rtParameters.SHARCAccumulationBufferIndex];
        sharcParameters.resolvedBuffer = ResourceDescriptorHeap[rtParameters.SHARCResolvedBufferIndex];
        sharcParameters.radianceScale = rtParameters.SHARCRadianceScale;
        sharcParameters.enableAntiFireflyFilter = rtParameters.SHARCEnableAntifirefly;
    }

    RESTIRRay indirectRay;
    indirectRay.Origin = 0;
    indirectRay.Direction = 0;
    indirectRay.HitPosition = 0;
    indirectRay.HitNormal = 0;
    indirectRay.HitRadiance = 0;
    indirectRay.proba = 1;
    /*
    if (!isUpdatePass)
        lighted[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    */
    
    
    const int samplesPerPixel = isUpdatePass ? 1 : rtParameters.SHARCSamplesPerPixel;
    for (int sampleIndex = 0; sampleIndex < samplesPerPixel; sampleIndex++)
    {
        // Initialize SHARC state
        SharcState sharcState;
        SharcInit(sharcState);

        /*
        float2 pixel = float2(launchIndex);
        pixel += (g_Global.enableJitter || isUpdatePass) ? float2(RNG(rngState), RNG(rngState)) : 0.5f.xx;
        RayDesc ray = GeneratePinholeCameraRay(pixel / launchDimensions,
            isUpdatePass ? g_Lighting.updatePassView.matViewToWorld : g_Lighting.view.matViewToWorld,
            isUpdatePass ? g_Lighting.updatePassView.matViewToClip : g_Lighting.view.matViewToClip);
        */
        RayDesc ray;
        ray.Origin = cd.camera.worldPos.xyz;
        ray.Direction = cd.viewDir;
        ray.TMin = 0.0f;
        ray.TMax = TRACING_DISTANCE;

        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);

        float materialRoughnessPrev = 0.0f;
        bool isDiffusePath = true; // Used by denoiser
        float hitDistance = 0.0f; // Used by denoiser

        bool internalRay = false;
        
        SurfaceData s;
        
        int bounce;
        for (bounce = 0; true /* break from the middle */; bounce++)
        {
            RayPayload payload;
            payload.hitDistance = -1.0f;
            payload.instanceID = ~0U;
            payload.primitiveIndex = ~0U;
            payload.geometryIndex = ~0U;
            payload.barycentrics = 0;
            
            float3 hitPos = 0;
            
            if (bounce == 0) // Primary ray use GBuffer surface attributes
            {
                payload.hitDistance = cd.viewDist * 0.995f;
                payload.instanceID = ~0U;
                payload.primitiveIndex = ~0U;
                payload.geometryIndex = ~0U;
                payload.barycentrics = 0;
                hitPos = cd.worldPos.xyz - cd.viewDir * 0.001 * cd.viewDist + cd.worldNorm * 0.001 * cd.viewDist;
                
                s = GetSurfaceData(pixel);
            }
            else
            {
                uint rayFlags = (!rtParameters.enableBackFaceCull || internalRay) ? RAY_FLAG_NONE : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

#if DISABLE_BACK_FACE_CULLING
                rayFlags &= (~RAY_FLAG_CULL_BACK_FACING_TRIANGLES);
#endif // DISABLE_BACK_FACE_CULLING
                
                TraceRay(AccelerationStructure, rayFlags, 0xFF, 0, 0, 0, ray, payload);
                hitPos = ray.Origin + ray.Direction * payload.hitDistance;
                
                s = GetRTSurfaceData(payload.instanceID, payload.primitiveIndex, payload.geometryIndex, payload.barycentrics);
            }

#if SHARC_UPDATE
            // SHaRC handles radiance propagation per segment; no path throughput needed.
            // Reset throughput to 1 at each bounce.
            sampleRadiance = float3(0.0f, 0.0f, 0.0f);
            throughput = float3(1.0f, 1.0f, 1.0f);
#endif // SHARC_UPDATE

            if (bounce == 1)
            {
                hitDistance = payload.Hit() ? payload.hitDistance : TRACING_DISTANCE;
            }

            // On a miss, load the sky value and break out of the ray tracing loop
            if (!payload.Hit())
            {
                float3 skyValue = Sky(ray.Direction);

                SharcUpdateMiss(sharcParameters, sharcState, skyValue);

                sampleRadiance += skyValue * throughput;

                break;
            }

            // Flip normals towards the incident ray direction (needed for backfacing triangles)
            float3 viewVector = -ray.Direction;

            // Flip the triangle normal, based on positional data, NOT the provided vertex normal
            float3 geometryNormal = s.objectNormal;
            if (dot(geometryNormal, viewVector) < 0.0f)
                geometryNormal = -geometryNormal;

            // Flip the shading normal, based on texture
            float3 shadingNormal = s.normal;
            if (dot(geometryNormal, shadingNormal) < 0.0f)
                shadingNormal = -shadingNormal;


            // Construct SharcHitData structure needed for creating a query point at this hit location
            SharcHitData sharcHitData;
            sharcHitData.positionWorld = hitPos;
            sharcHitData.normalWorld = geometryNormal;
#if SHARC_MATERIAL_DEMODULATION
            sharcHitData.materialDemodulation = float3(1.0f, 1.0f, 1.0f);
            if (g_Global.sharcMaterialDemodulation)
            {
                float3 specularFAvg = s.specularF0 + (1.0f - s.specularF0) * 0.047619; // 1/21
                sharcHitData.materialDemodulation = max(s.diffuseAlbedo, 0.05f) + max(s.specularF0, 0.02f) * luminance(specularFAvg);
            }
#endif // SHARC_MATERIAL_DEMODULATION
#if SHARC_SEPARATE_EMISSIVE
            sharcHitData.emissive = s.emissiveColor;
#endif // SHARC_SEPARATE_EMISSIVE

#if SHARC_UPDATE
            s.roughness = max(rtParameters.SHARCRoughnessThreshold, s.roughness);
#endif // SHARC_UPDATE

#if SHARC_QUERY
            
            if(bounce == 1)
            {
                indirectRay.Origin = ray.Origin;
                indirectRay.Direction = ray.Direction;
                indirectRay.HitPosition = hitPos;
                indirectRay.HitNormal = s.normal;
                indirectRay.proba = 1;
            }
            
            {
                uint gridLevel = HashGridGetLevel(hitPos, sharcParameters.gridParameters);
                float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.gridParameters);
                bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);

                materialRoughnessPrev = min(materialRoughnessPrev, 0.99f);
                float alpha = materialRoughnessPrev * materialRoughnessPrev;
                float footprint = payload.hitDistance * sqrt(0.5f * alpha * alpha / (1.0f - alpha * alpha));
                isValidHit &= footprint > voxelSize;

                float3 sharcRadiance;
                if (isValidHit && SharcGetCachedRadiance(sharcParameters, sharcHitData, sharcRadiance, false))
                {
                    sampleRadiance += sharcRadiance * throughput;

                    break; // Terminate the path once we've looked up into the cache
                }

#if SHARC_ENABLE_DEBUG
                if (g_Global.sharcDebug)
                {
                    float3 debugColor;
                    SharcGetCachedRadiance(sharcParameters, sharcHitData, debugColor, true);
                    //debugColor = HashGridDebugHashCollisions(sharcHitData.positionWorld, sharcHitData.normalWorld, sharcParameters.gridParameters, sharcParameters.hashMapData);

                    u_Output[DispatchRaysIndex().xy] = float4(debugColor, 1.0f);

                    return;
                }
#endif // SHARC_ENABLE_DEBUG
            }
#endif // SHARC_QUERY
            
            if (rtParameters.enableLighting)
            {
                // Evaluate direct light (next event estimation), start by sampling one light 
                HLSL::Light light;
                float lightWeight;

                if (SampleLightRIS(rngState, hitPos, geometryNormal, light, lightWeight, AccelerationStructure))
                {
                    // Prepare data needed to evaluate the light
                    float3 incidentVector;
                    float lightDistance;
                    float irradiance;
                    float2 rand2 = float2(RNG(rngState), RNG(rngState));
                    GetLightData(light, hitPos, rand2, rtParameters.enableSoftShadows, incidentVector, lightDistance, irradiance);
                    float3 vectorToLight = -incidentVector;

                    // Cast shadow ray towards the selected light
                    float3 lightVisibility = CastShadowRay(AccelerationStructure, hitPos, geometryNormal, vectorToLight, lightDistance);
                    if (any(lightVisibility > 0.0f))
                    {
                        // If light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
                        // This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
                        float3 lightContribution = evalCombinedBRDF(shadingNormal, vectorToLight, viewVector, s) * light.color.xyz * irradiance * lightWeight * lightVisibility;
                        sampleRadiance += lightContribution * throughput;
                    }
                }
            }

            // Terminate the loop early on the last bounce (we don't need to sample the BRDF)
            if (bounce == rtParameters.bouncesMax - 1)
            {
                break;
            }

#if !(SHARC_UPDATE && SHARC_SEPARATE_EMISSIVE)
            sampleRadiance += s.emissiveColor * throughput;
#endif

            if (!SharcUpdateHit(sharcParameters, sharcState, sharcHitData, sampleRadiance, RNG(rngState)))
                break;

            // Russian roulette
            if (rtParameters.enableRussianRoulette && (bounce > BOUNCES_MIN))
            {
                float rrProbability = min(0.95f, luminance(throughput));
                const bool terminate = (rrProbability < RNG(rngState));
                if (terminate)
                    break;
                else
                    throughput /= rrProbability;
            }

            // Sample BRDF to generate the next ray
            // First, figure out whether to sample diffuse or specular BRDF
            int brdfType = DIFFUSE_TYPE;

            float specularBrdfProbability = GetSpecularBrdfProbability(s, viewVector, shadingNormal);
            if (RNG(rngState) < specularBrdfProbability)
            {
                brdfType = SPECULAR_TYPE;
                throughput /= specularBrdfProbability;
            }
            else if (rtParameters.enableTransmission)
            {
                float transmissiveProbability = (1.0f - specularBrdfProbability) * s.transmission;

                if (RNG(rngState) < s.transmission)
                {
                    brdfType = TRANSMISSIVE_TYPE;
                    throughput /= transmissiveProbability;
                }
                else
                {
                    brdfType = DIFFUSE_TYPE;
                    throughput /= (1.0f - specularBrdfProbability - transmissiveProbability);
                }
            }
            else
            {
                brdfType = DIFFUSE_TYPE;
                throughput /= (1.0f - specularBrdfProbability);
            }

#if SHARC_QUERY
            materialRoughnessPrev += brdfType == DIFFUSE_TYPE ? 1.0f : s.roughness;
#endif // SHARC_QUERY

            // Run importance sampling of selected BRDF to generate reflecting ray direction
            float3 brdfWeight = float3(0.0f, 0.0f, 0.0f);
            float brdfPdf = 0.0f;
            float refractiveIndex = 1.0f; // ior //1.1f;

            // Generates a new ray direction
            float2 rand2 = float2(RNG(rngState), RNG(rngState));
            if (!evalIndirectCombinedBRDF(rand2, shadingNormal, geometryNormal, viewVector, s, brdfType, refractiveIndex, ray.Direction, brdfWeight, brdfPdf))
            {
                break; // Ray was eaten by the surface :(
            }

            // Refraction requires the ray offset to go in the opposite direction
            bool transition = dot(geometryNormal, ray.Direction) <= 0.0f;
            ray.Origin = OffsetRay(hitPos, transition ? -geometryNormal : geometryNormal);

            // If we are internal, assume we will be leaving the object on a transition and air has an ior of ~1.0
            if (internalRay)
            {
                refractiveIndex = 1.0f / refractiveIndex;

                //if (g_Global.enableAbsorbtion)
                //    throughput *= exp(-0.5f * payload.hitDistance); // Beers law of attenuation
            }

            if (transition)
                internalRay = !internalRay;
            
            throughput *= s.occlusion;

            // Account for surface properties using the BRDF "weight"
            throughput *= brdfWeight;

            SharcSetThroughput(sharcState, throughput);
            if (!isUpdatePass && luminance(throughput) < rtParameters.throughputThreshold)
                break;
        }

        if (!isUpdatePass)
        {
            UpdateSampleData(accumulatedSampleData, sampleRadiance, isDiffusePath, hitDistance);
            
            if (editorContext.GIBounces)// Bounce Heatmap
                debugColor = BounceHeatmap(bounce);
        }
    }

    // Don't write any output when we're just updating a radiance cache
    if (isUpdatePass)
        return;

    // Write radiance to output buffer
    float4 color = ResolveSampleData(accumulatedSampleData, rtParameters.SHARCSamplesPerPixel, rtParameters.SHARCRadianceScale);
    
    if (editorContext.GIBounces)// Bounce Heatmap
        color = float4(debugColor, 1);
    
    indirectRay.HitRadiance = color.xyz;
    RESTIR(rtParameters, indirectRay, rtParameters.previousgiReservoirIndex, rtParameters.giReservoirIndex, cd, RNG(rngState));

    /*
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    HLSL::GIReservoirCompressed res = {};
    res.color = PackRGBE_sqrt(color.xyz);
    giReservoir[cd.pixel.x + cd.pixel.y * viewContext.renderResolution.x] = res;
    */
    
    /*
    // Debug output calculation
    if (g_Global.debugOutputMode != 0)
    {
        float2 pixel = float2(DispatchRaysIndex().xy) + 0.5.xx;
        RayDesc ray = GeneratePinholeCameraRay(pixel / float2(launchDimensions), g_Lighting.view.matViewToWorld, g_Lighting.view.matViewToClip);
        RayPayload payload = (RayPayload)0;
        TraceRay(SceneBVH, 0, 0xFF, 0, 0, 0, ray, payload);

        if (payload.Hit())
        {
            GeometrySample geometry = getGeometryFromHit(payload.instanceID, payload.primitiveIndex, payload.geometryIndex, payload.barycentrics, GeomAttr_All, t_InstanceData, t_GeometryData, t_MaterialConstants);

            if (g_Global.debugOutputMode == 1)// DiffuseReflectance
            {
                MaterialSample material = SampleGeometryMaterial(geometry, 0, 0, 0, MatAttr_All, s_MaterialSampler, t_BindlessTextures);
                debugColor = s.diffuseAlbedo;
            }
            else if (g_Global.debugOutputMode == 2 ) // WorldSpaceNormals
            {
                debugColor = geometry.geometryNormal * 0.5f + 0.5f;
            }
            else if (g_Global.debugOutputMode == 3 )// WorldSpacePosition
            {
                debugColor = mul(geometry.instance.transform, float4(geometry.objectSpacePosition, 1.0f)).xyz;
            }
            else if (g_Global.debugOutputMode == 4 )// Barycentrics
            {
                debugColor = float3(1 - payload.barycentrics.x - payload.barycentrics.y, payload.barycentrics.x, payload.barycentrics.y);
            }
            else if (g_Global.debugOutputMode == 5 )// HitT
            {
                debugColor = float3(payload.hitDistance, payload.hitDistance, payload.hitDistance) / 100.0f;
            }
            else if (g_Global.debugOutputMode == 6 )// InstanceID
            {
                debugColor = HashAndColor(payload.instanceID);
            }
            else if (g_Global.debugOutputMode == 7 )// Emissives
            {
                MaterialSample material = SampleGeometryMaterial(geometry, 0, 0, 0, MatAttr_All, s_MaterialSampler, t_BindlessTextures);
                debugColor = s.emissiveColor;
            }
            else if (g_Global.debugOutputMode == 8 )// Heat map
            {
                // Already set
            }
        }

        u_Output[launchIndex] = float4(debugColor, 1.0f);
    }
    */
}

[shader("raygeneration")]
void RayGen()
{
    PathTraceRays();
}