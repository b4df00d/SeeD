#include "structs.hlsl"

#define SHARC_ENABLE_64_BIT_ATOMICS 1
#define TRACING_DISTANCE                10000.0f
#define SPECULAR_HITT_MAX               65504.0f // fp16 max: the specularHitDistance guide is R16_FLOAT, anything above becomes +Inf
#define BOUNCES_MIN                     0
#define BOUNCES_MAX                     10
#define RIS_CANDIDATES_LIGHTS           8 // Number of candidates used for resampling of analytical lights
#define SHADOW_RAY_IN_RIS               0 // Enable this to cast shadow rays for each candidate during resampling. This is expensive but increases quality
#define SHARC_SEPARATE_EMISSIVE         1
#define USE_SER                         1 // Shader Execution Reordering (SM 6.9): regroup threads by hit coherence after traversal, before the divergent shading

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
    AlphaTestAnyHit(attrib);
}


// Load-bearing: shadow rays trace with RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, so reaching
// the miss shader is the only signal that the ray was not occluded
[shader("miss")]
void MissShadow(inout ShadowRayPayload payload : SV_RayPayload)
{
    payload.missed = 1;
}

[shader("closesthit")]
void ClosestHitShadow(inout ShadowRayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.visibility = float3(0.0f, 0.0f, 0.0f);
}

[shader("anyhit")]
void AnyHitShadow(inout ShadowRayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    AlphaTestAnyHitShadow(attrib);
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

// Rebuilt per bounce instead of held across the loop: everything live over a TraceRay is spilled to
// memory around it (and moved with the thread by SER's MaybeReorderThread), while these are all
// constant-buffer values, descriptor indices and one camera load.
SharcParameters GetSharcParameters()
{
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];

    SharcParameters sharcParameters;
    sharcParameters.gridParameters.cameraPosition = cameras[0].worldPos.xyz;
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
    return sharcParameters;
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
    int debugBounce = 0; // bounce heatmap: the colour is only built at output time

    // DLSS-RR specular hit distance guide: world-space distance from the primary surface to
    // the first indirect hit. RR reconstructs the virtual reflection point
    // (surfacePos + viewDir * hitT) from it to reproject the specular history WITH parallax.
    // Feeding 0 makes it reproject reflections as if they were painted onto the surface, the
    // history never matches while the camera moves, and RR falls back to a wide spatial
    // filter -> reflections blur on motion and only resharpen once the camera stops.
    // Only samples whose primary lobe was specular measure the right distance; when none did,
    // a dedicated specular ray is traced after the sample loop (see there). The diffuse-lobe
    // distance is only kept as a fallback on rough surfaces, where the specular lobe is wide
    // and of the same scale (and RR weighs hitT less there anyway).
    float specularHitTSum = 0.0f;
    float anyHitTSum = 0.0f;
    uint hitTCounts = 0; // specular count << 16 | any count (fewer values live across TraceRay)
    // Direction of the dedicated hitT guide ray (see after the sample loop), sampled at bounce 0 where
    // the primary surface is already resolved: bit 31 = valid, 15:15 octahedral. 1 = sky pixel, 0 = not
    // decided yet. Re-resolving the surface after the loop (visibility buffer + material textures) cost
    // ~0.85 ms on its own, even when the roughness test then skipped the ray.
    uint specularGuideDir = 0;

    uint2 pixel = launchIndex;
#if (SHARC_UPDATE)
    float2 rand2 = float2(RNG(rngState), RNG(rngState));
    pixel = pixel / (float2)launchDimensions * (float2)viewContext.renderResolution + (rand2 * rtParameters.probeDownsampling);
#endif
    RaytracingAccelerationStructure AccelerationStructure = ResourceDescriptorHeap[rtParameters.BVH];
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

    float primaryRoughtness = 1;

    // First indirect segment, kept for temporal ReSTIR after the path loop. Only what RESTIR()
    // reads, not a full RESTIRRay (origin + hit position collapse to a distance, proba is 1).
    float3 restirDirection = 0;
    float3 restirHitNormal = 0;
    float restirHitDistance = 0;

        /*
        float2 pixel = float2(launchIndex);
        pixel += (g_Global.enableJitter || isUpdatePass) ? float2(RNG(rngState), RNG(rngState)) : 0.5f.xx;
        RayDesc ray = GeneratePinholeCameraRay(pixel / launchDimensions,
            isUpdatePass ? g_Lighting.updatePassView.matViewToWorld : g_Lighting.view.matViewToWorld,
            isUpdatePass ? g_Lighting.updatePassView.matViewToClip : g_Lighting.view.matViewToClip);
        */
        // Camera data is used here and recomputed after the path loop, NOT held across it: bounce 0
        // reads its hit distance from ray.TMax and rebuilds its offset hit position from the ray.
        // Temporal ReSTIR's reprojection inputs are evaluated here too (they don't depend on the path)
        // and carried as 2 dwords, rather than recomputing the camera data after the loop, where its
        // motion / previous-depth loads had nothing left to overlap with.
        RayDesc ray;
        uint restirPrevPixel;
        float restirReprojectionConfidence;
        {
            GBufferCameraData cd = GetGBufferCameraData(pixel);
            ray.Origin = cd.camera.worldPos.xyz;
            ray.Direction = cd.viewDir;
            ray.TMin = 0.01f;
            ray.TMax = cd.viewDist; // reset to TRACING_DISTANCE once bounce 0 has read it
            restirPrevPixel = PackPixel(uint2(cd.previousPixel));
            restirReprojectionConfidence = ReprojectionConfidence(cd);
            if (!(cd.reverseZ > 0.0))
                specularGuideDir = 1; // sky: no guide ray
        }

        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);

        float materialRoughnessPrev = 0.0f;
        bool isDiffusePath = true; // Used by denoiser
        float hitDistance = 0.0f; // Used by denoiser

        bool internalRay = false;

        OpenPBRSurface s;

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
                payload.hitDistance = ray.TMax;
                payload.instanceID = ~0U;
                payload.primitiveIndex = ~0U;
                payload.geometryIndex = ~0U;
                payload.barycentrics = 0;

                s = GetPrimarySurface(pixel);

                // == GBufferCameraData::offsetedWorldPos (s.normal is the same gbuffer normal as cd.worldNorm)
                float viewDist = ray.TMax;
                hitPos = ray.Origin + ray.Direction * viewDist - ray.Direction * 0.002 * viewDist + s.normal * 0.002 * viewDist;
                ray.TMax = TRACING_DISTANCE;

                primaryRoughtness = s.specular_roughness;
            }
            else
            {
                uint rayFlags = (!rtParameters.enableBackFaceCull || internalRay) ? RAY_FLAG_NONE : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

#if DISABLE_BACK_FACE_CULLING
                rayFlags &= (~RAY_FLAG_CULL_BACK_FACING_TRIANGLES);
#endif // DISABLE_BACK_FACE_CULLING
                //rayFlags &= (~RAY_FLAG_FORCE_OPAQUE);
                //rayFlags &= (~RAY_FLAG_FORCE_NON_OPAQUE);

#if USE_SER
                dx::HitObject hit = dx::HitObject::TraceRay(AccelerationStructure, rayFlags, 0xFF, 0, 0, 0, ray, payload);
                dx::MaybeReorderThread(hit);
                // read the hit straight from the HitObject instead of invoking the trivial
                // closesthit/miss shaders; the payload is pre-initialized to the miss values
                if (hit.IsHit())
                {
                    payload.instanceID = hit.GetInstanceID();
                    payload.primitiveIndex = hit.GetPrimitiveIndex();
                    payload.geometryIndex = hit.GetGeometryIndex();
                    Attributes hitAttribs;
                    hit.GetAttributes(hitAttribs);
                    payload.barycentrics = hitAttribs.uv;
                    uint packedDistance = asuint(hit.GetRayTCurrent()) & (~0x1u);
                    packedDistance |= hit.GetHitKind() == HIT_KIND_TRIANGLE_FRONT_FACE ? 0x1 : 0x0;
                    payload.hitDistance = asfloat(packedDistance);
                }
#else
                TraceRay(AccelerationStructure, rayFlags, 0xFF, 0, 0, 0, ray, payload);
#endif
                hitPos = ray.Origin + ray.Direction * payload.hitDistance;

                s = GetRTSurface(payload.instanceID, payload.primitiveIndex, payload.barycentrics);
            }

#if SHARC_UPDATE
            // SHaRC handles radiance propagation per segment; no path throughput needed.
            // Reset throughput to 1 at each bounce.
            sampleRadiance = float3(0.0f, 0.0f, 0.0f);
            throughput = float3(1.0f, 1.0f, 1.0f);
#endif // SHARC_UPDATE

            if(bounce == 1)
            {
                restirDirection = ray.Direction;
                restirHitNormal = s.normal;
                restirHitDistance = length(hitPos - ray.Origin);
            }

            hitDistance = payload.Hit() ? payload.hitDistance : TRACING_DISTANCE * 100;

            // Distance from the primary surface to the first indirect hit -> DLSS-RR guide.
            // Clamped to the ray budget (not TRACING_DISTANCE * 100) so the R16_FLOAT target
            // keeps a finite value on a miss.
            if (bounce == 1)
            {
                float firstBounceHitT = payload.Hit() ? payload.hitDistance : TRACING_DISTANCE;
                anyHitTSum += firstBounceHitT;
                hitTCounts += 1;
                if (!isDiffusePath)
                {
                    specularHitTSum += firstBounceHitT;
                    hitTCounts += 1u << 16;
                }
            }

            SharcParameters sharcParameters = GetSharcParameters();

            // On a miss, load the sky value and break out of the ray tracing loop
            if (!payload.Hit())
            {
                float3 skyValue = Sky(ray.Direction);

                SharcUpdateMiss(sharcParameters, sharcState, skyValue);

                sampleRadiance += skyValue * throughput;

                hitPos = ray.Origin + ray.Direction * TRACING_DISTANCE * 100;

                break;
            }

            float3 viewVector = -ray.Direction;
            if (!rtParameters.enableTransmission)
                s.transmission_weight = 0.0f;
#if SHARC_UPDATE
            // the cache stores diffuse-ish radiance: no sharp lobes feeding it
            s.specular_roughness = max(rtParameters.SHARCRoughnessThreshold, s.specular_roughness);
            s.coat_roughness = max(rtParameters.SHARCRoughnessThreshold, s.coat_roughness);
#endif // SHARC_UPDATE
            // OpenPBRPrepare turns the normals toward the viewer (back faces, normal maps)
            OpenPBRData pbr = OpenPBRPrepare(s, viewVector);
            float3 geometryNormal = pbr.Ng;

            // hitT guide direction, picked before any of the breaks below (the ray is only traced
            // after the loop, if no sample took the specular lobe)
            if (!isUpdatePass && bounce == 0 && specularGuideDir == 0
                && s.specular_roughness <= rtParameters.specularHitTRayRoughnessMax && rtParameters.specularHitTRayRoughnessMax > 0.0f)
            {
                float3 guideDir;
                float2 guideRand = float2(RNG(rngState), RNG(rngState));
                if (OpenPBRSampleReflectionDirection(pbr, guideRand, guideDir) && dot(geometryNormal, guideDir) > 0.0f)
                    specularGuideDir = 0x80000000u | octahedral_32(guideDir, 15);
            }


            // Construct SharcHitData structure needed for creating a query point at this hit location
            SharcHitData sharcHitData;
            sharcHitData.positionWorld = hitPos;
            sharcHitData.normalWorld = geometryNormal;
#if SHARC_MATERIAL_DEMODULATION
            {
                float3 diffuseAlbedo, specularAlbedo;
                OpenPBRGuides(pbr, diffuseAlbedo, specularAlbedo);
                sharcHitData.materialDemodulation = max(diffuseAlbedo, 0.05f) + max(specularAlbedo, 0.02f);
            }
#endif // SHARC_MATERIAL_DEMODULATION
#if SHARC_SEPARATE_EMISSIVE
            sharcHitData.emissive = pbr.emission;
#endif // SHARC_SEPARATE_EMISSIVE

//#if SHARC_QUERY
#ifndef SHARC_UPDATE // == SHARC_QUERY for now, better help with having the code not greyed

            //if (bounce > BOUNCES_MIN)
            {
                uint gridLevel = HashGridGetLevel(hitPos, sharcParameters.gridParameters);
                float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.gridParameters);
                bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);
                //isValidHit = false;

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
                    float lightFalloff;
                    float2 rand2 = float2(RNG(rngState), RNG(rngState));
                    GetLightData(light, hitPos, rand2, rtParameters.enableSoftShadows, incidentVector, lightDistance, irradiance, lightFalloff);
                    // Apply distance attenuation once, after RIS selection (see GetLightData comment).
                    irradiance *= lightFalloff;
                    float3 vectorToLight = -incidentVector;

                    // Cast shadow ray towards the selected light
                    float3 lightVisibility = light.castShadow ? CastShadowRay(AccelerationStructure, hitPos, geometryNormal, vectorToLight, lightDistance) : float3(1, 1, 1);
                    if (any(lightVisibility > 0.0f))
                    {
                        // If light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
                        // This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
                        float3 lightContribution = light.color.xyz * irradiance * lightWeight * lightVisibility;
                        // Directional lights are defined at the top of the atmosphere (as the sky is)
                        if (light.type == HLSL::LightType::Directional && viewContext.atmosphere.transmittanceLUT != HLSL::invalidUINT)
                            lightContribution *= SampleTransmittance(viewContext.atmosphere, AtmospherePosition(viewContext.atmosphere, hitPos), vectorToLight);
                        float bsdfPdf;
                        lightContribution *= OpenPBREval(pbr, vectorToLight, bsdfPdf);

#ifndef SHARC_UPDATE // == SHARC_QUERY for now, better help with having the code not greyed
                        if (bounce == 0)
                            accumulatedSampleData.radiance += lightContribution; // direct light (UpdateSampleData is a plain +=)
                        else
#endif // !(SHARC_UPDATE)
                            sampleRadiance += lightContribution * throughput;
                    }
                }
            }

            // Terminate the loop early on the last bounce (we don't need to sample the BRDF)
            //if (bounce > rtParameters.bouncesMax)
            if(bounce > BOUNCES_MAX)
            {
                break;
            }

#if !(SHARC_UPDATE && SHARC_SEPARATE_EMISSIVE)
#ifndef SHARC_UPDATE // == SHARC_QUERY
            // The primary surface's own emission is direct light, like the NEE above: sampleRadiance
            // becomes the ReSTIR GI sample, reprojected and shared between pixels, which would drag
            // the emitter's colour across the screen whenever the camera or the object moves.
            if (bounce == 0)
                accumulatedSampleData.radiance += pbr.emission;
            else
#endif
                sampleRadiance += pbr.emission * throughput;
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

            // Sample the BSDF for the next ray
            float3 bsdfWeight;
            float bsdfPdf;
            uint bsdfFlags;
            float lobeRoughness;
            if (!OpenPBRSample(pbr, float3(RNG(rngState), RNG(rngState), RNG(rngState)), ray.Direction, bsdfWeight, bsdfPdf, bsdfFlags, lobeRoughness))
                break; // the sample was absorbed

            bool transition = (bsdfFlags & OpenPBRSampleTransmission) != 0;
            // a reflection the shading normal sent under the triangle: lost
            if (!transition && dot(geometryNormal, ray.Direction) <= 0.0f)
                break;

            // The lobe picked off the primary surface decides whether the bounce-1 hit distance
            // recorded above counts as a specular hit distance for DLSS-RR.
            if (bounce == 0)
                isDiffusePath = (bsdfFlags & OpenPBRSampleGlossy) == 0;

//#if SHARC_QUERY
#ifndef SHARC_UPDATE // == SHARC_QUERY for now, better help with having the code not greyed
            materialRoughnessPrev += lobeRoughness;
#endif // SHARC_QUERY

            // Transmission requires the ray offset to go in the opposite direction
            ray.Origin = OffsetRay(hitPos, transition ? -geometryNormal : geometryNormal);
            // inside a closed medium the back faces are the ones to hit
            if (bsdfFlags & OpenPBRSampleCrossesMedium)
                internalRay = !internalRay;

            throughput *= bsdfWeight;

            SharcSetThroughput(sharcState, throughput);
            if (!isUpdatePass && luminance(throughput) < rtParameters.throughputThreshold * 0.00001f)
                break;
        }

        if (!isUpdatePass)
        {
            RESTIRRay indirectRay;
            indirectRay.Origin = 0;
            indirectRay.Direction = restirDirection;
            indirectRay.HitPosition = restirDirection * restirHitDistance; // RESTIR only uses |HitPosition - Origin|
            indirectRay.HitNormal = restirHitNormal;
            indirectRay.HitRadiance = sampleRadiance;
            indirectRay.proba = 1;

            // Always run temporal ReSTIR so the reservoir is populated for the spatial
            // reuse pass (lighting.hlsl), even when temporal filtering is disabled
            // (maxFrameFilteringCount == 1 -> the reservoir just holds this frame's sample).
            RESTIR(rtParameters, indirectRay, rtParameters.previousgiReservoirIndex, rtParameters.giReservoirIndex, pixel, UnpackPixel(restirPrevPixel), restirReprojectionConfidence, rngState, 1-primaryRoughtness);

            // Pass 1 composites DIRECT light only (added into accumulatedSampleData at bounce 0).
            // The indirect radiance is resolved from the reservoir AFTER spatial reuse in
            // lighting.hlsl, which does lighted += indirect.

            if (editorContext.debugFlags & HLSL::EditorDebugGIBounces)// Bounce Heatmap
                debugBounce = bounce;
        }
    }

    // Don't write any output when we're just updating a radiance cache
    if (isUpdatePass)
        return;

    // Write radiance to output buffer
    // No intensity scale: SHARCRadianceScale is SHARC's fixed-point factor (SharcCommon.h), not an output gain
    float4 color = ResolveSampleData(accumulatedSampleData, rtParameters.SHARCSamplesPerPixel, 1.0f);



    if (editorContext.debugFlags & HLSL::EditorDebugGIBounces)// Bounce Heatmap
        color = float4(BounceHeatmap(debugBounce), 1);
    if (editorContext.debugFlags & (HLSL::EditorDebugGIAlbedo | HLSL::EditorDebugGINormals))
    {
        GBufferCameraData cd = GetGBufferCameraData(pixel);
        RayDesc ray;
        ray.Origin = cd.camera.worldPos.xyz;
        ray.Direction = cd.viewDir;
        ray.TMin = 0.01f;
        ray.TMax = TRACING_DISTANCE;

        RayPayload payload;
        payload.hitDistance = -1.0f;
        payload.instanceID = ~0U;
        payload.primitiveIndex = ~0U;
        payload.geometryIndex = ~0U;
        payload.barycentrics = 0;

        uint rayFlags = (!rtParameters.enableBackFaceCull) ? RAY_FLAG_NONE : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

#if DISABLE_BACK_FACE_CULLING
    rayFlags &= (~RAY_FLAG_CULL_BACK_FACING_TRIANGLES);
#endif // DISABLE_BACK_FACE_CULLING
    //rayFlags &= (~RAY_FLAG_FORCE_OPAQUE);

        TraceRay(AccelerationStructure, rayFlags, 0xFF, 0, 0, 0, ray, payload);

        OpenPBRSurface s = GetRTSurface(payload.instanceID, payload.primitiveIndex, payload.barycentrics);
        OpenPBRData pbr = OpenPBRPrepare(s, -cd.viewDir);

        if (editorContext.debugFlags & HLSL::EditorDebugGIAlbedo)
        {
            float3 diffuseAlbedo, specularAlbedo;
            OpenPBRGuides(pbr, diffuseAlbedo, specularAlbedo);
            color = float4(diffuseAlbedo + specularAlbedo, 1);
        }
        if (editorContext.debugFlags & HLSL::EditorDebugGINormals)
            color = float4(pbr.N * 0.5 + 0.5, 1);
    }


    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    RWTexture2D<float> specularHitDistance = ResourceDescriptorHeap[rtParameters.specularHitDistanceIndex];
    lighted[pixel] = color;

    uint specularHitTCount = hitTCounts >> 16;
    uint anyHitTCount = hitTCounts & 0xFFFF;

    // No sample picked the specular lobe off the primary surface: trace one ray along a
    // specular sample just for the guide. Dielectrics pick that lobe ~10% of the time
    // (in proportion to their reflected energy), so on most frames the guide used to carry a
    // random cosine-lobe distance instead, RR reprojected the reflection history to a
    // different virtual point every frame, rejected it and blurred reflections under motion.
    // Direction sampled at bounce 0 (specularGuideDir); only the origin is rebuilt here.
    if (specularHitTCount == 0 && (specularGuideDir & 0x80000000u))
    {
        GBufferCameraData cd = GetGBufferCameraData(pixel);
        RayDesc ray;
        ray.Direction = normalize(i_octahedral_32(specularGuideDir & 0x3FFFFFFFu, 15));
        // offset along the gbuffer normal (bounce 0 used the triangle normal, close enough for a guide)
        ray.Origin = OffsetRay(cd.offsetedWorldPos, cd.worldNorm);
        ray.TMin = 0.01f;
        ray.TMax = TRACING_DISTANCE;
        uint rayFlags = (!rtParameters.enableBackFaceCull) ? RAY_FLAG_NONE : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
#if DISABLE_BACK_FACE_CULLING
        rayFlags &= (~RAY_FLAG_CULL_BACK_FACING_TRIANGLES);
#endif // DISABLE_BACK_FACE_CULLING
        if (rtParameters.specularHitTRayOpaque)
            rayFlags |= RAY_FLAG_FORCE_OPAQUE; // distance guide only: cutouts as solid, no any-hit

        RayPayload payload;
        payload.hitDistance = -1.0f;
        payload.instanceID = ~0U;
        payload.primitiveIndex = ~0U;
        payload.geometryIndex = ~0U;
        payload.barycentrics = 0;
#if USE_SER
        // only the distance is needed: no reorder, no closesthit/miss invocation
        dx::HitObject hit = dx::HitObject::TraceRay(AccelerationStructure, rayFlags, 0xFF, 0, 0, 0, ray, payload);
        specularHitTSum = hit.IsHit() ? hit.GetRayTCurrent() : TRACING_DISTANCE;
#else
        TraceRay(AccelerationStructure, rayFlags, 0xFF, 0, 0, 0, ray, payload);
        specularHitTSum = payload.Hit() ? payload.hitDistance : TRACING_DISTANCE;
#endif
        specularHitTCount = 1;
    }

    // Prefer the specular-lobe distance, fall back to the diffuse-lobe first-bounce distance
    // (rough surfaces, or the specular sample went under the surface), and only write 0
    // ("reflection sits on the surface") when the path never left the primary hit.
    // lighting.hlsl overwrites this for sky pixels and must not clear it for the rest.
    float outSpecularHitT = specularHitTCount > 0 ? specularHitTSum / specularHitTCount
                          : (anyHitTCount > 0 ? anyHitTSum / anyHitTCount : 0.0f);
    specularHitDistance[pixel] = min(outSpecularHitT, SPECULAR_HITT_MAX);


    /*
    // Debug output calculation
    if (editorContext.GIAlbedo)
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