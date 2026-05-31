#include "structs.hlsl"

cbuffer CustomRT : register(b3)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"


#define SHARC_ENABLE_64_BIT_ATOMICS 1
#define TRACING_DISTANCE                10000.0f
#define BOUNCES_MIN                     3
#define RIS_CANDIDATES_LIGHTS           8 // Number of candidates used for resampling of analytical lights
#define SHADOW_RAY_IN_RIS               0 // Enable this to cast shadow rays for each candidate during resampling. This is expensive but increases quality
#define ENABLE_SPECULAR_LOBE            1 // Enable to use the specular lobe for splitting between diffuse and specular BRDFs
#define SHARC_SEPARATE_EMISSIVE         1

#include "raytracingCommon.hlsl"

static uint poissonDiskCount = 4;
static float2 poissonDisk[64] = 
{
    float2(-0.613392, 0.617481),
    float2(0.170019, -0.040254),
    float2(-0.299417, 0.791925),
    float2(0.645680, 0.493210),
    float2(-0.651784, 0.717887),
    float2(0.421003, 0.027070),
    float2(-0.817194, -0.271096),
    float2(-0.705374, -0.668203),
    float2(0.977050, -0.108615),
    float2(0.063326, 0.142369),
    float2(0.203528, 0.214331),
    float2(-0.667531, 0.326090),
    float2(-0.098422, -0.295755),
    float2(-0.885922, 0.215369),
    float2(0.566637, 0.605213),
    float2(0.039766, -0.396100),
    float2(0.751946, 0.453352),
    float2(0.078707, -0.715323),
    float2(-0.075838, -0.529344),
    float2(0.724479, -0.580798),
    float2(0.222999, -0.215125),
    float2(-0.467574, -0.405438),
    float2(-0.248268, -0.814753),
    float2(0.354411, -0.887570),
    float2(0.175817, 0.382366),
    float2(0.487472, -0.063082),
    float2(-0.084078, 0.898312),
    float2(0.488876, -0.783441),
    float2(0.470016, 0.217933),
    float2(-0.696890, -0.549791),
    float2(-0.149693, 0.605762),
    float2(0.034211, 0.979980),
    float2(0.503098, -0.308878),
    float2(-0.016205, -0.872921),
    float2(0.385784, -0.393902),
    float2(-0.146886, -0.859249),
    float2(0.643361, 0.164098),
    float2(0.634388, -0.049471),
    float2(-0.688894, 0.007843),
    float2(0.464034, -0.188818),
    float2(-0.440840, 0.137486),
    float2(0.364483, 0.511704),
    float2(0.034028, 0.325968),
    float2(0.099094, -0.308023),
    float2(0.693960, -0.366253),
    float2(0.678884, -0.204688),
    float2(0.001801, 0.780328),
    float2(0.145177, -0.898984),
    float2(0.062655, -0.611866),
    float2(0.315226, -0.604297),
    float2(-0.780145, 0.486251),
    float2(-0.371868, 0.882138),
    float2(0.200476, 0.494430),
    float2(-0.494552, -0.711051),
    float2(0.612476, 0.705252),
    float2(-0.578845, -0.768792),
    float2(-0.772454, -0.090976),
    float2(0.504440, 0.372295),
    float2(0.155736, 0.065157),
    float2(0.391522, 0.849605),
    float2(-0.620106, -0.328104),
    float2(0.789239, -0.419965),
    float2(-0.545396, 0.538133),
    float2(-0.178564, -0.596057)
};


#pragma raytracing Lighting

GlobalRootSignature SeeDRootSignatureRT =
{
    SeeDRootSignature
};

[shader("raygeneration")]
void RayGen()
{
    //return;
    int2 dtid = DispatchRaysIndex().xy;
    if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y) return;
    
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    RWTexture2D<float> specularHitDistance = ResourceDescriptorHeap[rtParameters.specularHitDistanceIndex];
    //lighted[dtid.xy] = float4(0,0,0,1);
    specularHitDistance[dtid.xy] = 0;
    
    
    GBufferCameraData cd = GetGBufferCameraData(dtid);
    if(cd.reverseZ <= 0.0)
    {
        lighted[dtid.xy] = float4(Sky(cd.viewDir), 1) * 0.1;
        specularHitDistance[dtid.xy] = 10000 * 100;
        return;
    }
    SurfaceData s = GetSurfaceData(dtid.xy);
    
    uint seed = initRand(dtid.xy);
    uint poissonIndexRng = nextRand(seed) * (64 - poissonDiskCount);
    
    RaytracingAccelerationStructure AccelerationStructure = ResourceDescriptorHeap[rtParameters.BVH];
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    
    float2 radius = rtParameters.spacialRadius;
    float3 success = float3(0, 0, 1);
    if(radius.x > 0)
    {
        uint spacialReuse = 0;
        HLSL::GIReservoir candidateReservoir = r;
        float3 candidateRayOrigin = cd.offsetedWorldPos;
        for (uint i = 0; i < poissonDiskCount; i++)
        {
            int2 pixel = dtid.xy + poissonDisk[i+poissonIndexRng] * radius * (i / float(poissonDiskCount));
            if (pixel.x < 0 || pixel.y < 0) continue;
            if (pixel.x >= viewContext.renderResolution.x || pixel.y >= viewContext.renderResolution.y) continue;
            {
                GBufferCameraData cdNeightbor = GetGBufferCameraData(pixel.xy);
            
                if(abs(cd.viewDist - cdNeightbor.viewDist) > (0.2)) continue;
                if(dot(cd.worldNorm, cdNeightbor.worldNorm) < 0.9) continue;
                
                HLSL::GIReservoir rNeightbor = UnpackGIReservoir(giReservoir[pixel.x + pixel.y * viewContext.renderResolution.x]);
                
                if(rNeightbor.W > candidateReservoir.W)
                {
                    candidateReservoir.color = rNeightbor.color;
                    candidateReservoir.W = rNeightbor.W;
                    candidateReservoir.dist = rNeightbor.dist;
                    candidateReservoir.dir = rNeightbor.dir;
                    
                    candidateRayOrigin = cd.offsetedWorldPos;
                }
                candidateReservoir.Wsum += rNeightbor.Wsum;
                candidateReservoir.Wcount += rNeightbor.Wcount;
                
                spacialReuse++;
            }
        }
        
        if(spacialReuse > 0)
        {
            float3 candidateRayHitPos = candidateRayOrigin + candidateReservoir.dir * candidateReservoir.dist;
            float3 newRayDir = candidateRayHitPos - cd.offsetedWorldPos;
            float newRayDist = length(newRayDir);
            newRayDir = newRayDir / newRayDist;
        
            RayDesc ray;
            ray.Origin = cd.offsetedWorldPos;
            ray.Direction = newRayDir;
            ray.TMin = 0.0f;
            ray.TMax = newRayDist;

            ShadowRayPayload payload;
            payload.visibility = float3(1.0f, 1.0f, 1.0f);

            uint rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
            //rayFlags &= (~RAY_FLAG_FORCE_OPAQUE);
            TraceRay(AccelerationStructure, rayFlags, 0xFF, SHADOW_RAY_INDEX, 0, SHADOW_RAY_INDEX, ray, payload);

            if(any(payload.visibility > 0))
            {
                // we dont have visibility between current pixel and neightbor hitpoint, so we discard the sample
                r = candidateReservoir;
                r.dir = ray.Direction;
                r.dist = ray.TMax;
                success = float3(0, 1, 0);
                
                // If light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
                // This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
                //s.albedo = 1;
                //r.color = evalCombinedBRDF(cd.worldNorm, r.dir, -cd.viewDir, s) * r.color;
            }
            else
            {
                success = float3(1, 0, 0);
            }
        }
    }
    
    r.color = r.color / max(0.00001, r.W / (r.Wsum / r.Wcount));
    //r.color = saturate(evalCombinedBRDF(cd.worldNorm, r.dir, -cd.viewDir, s)) * r.color;
    
    float4 result = float4(r.color, 1);
    if (editorContext.rays) // daw ray
    {
        uint2 debugPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
        if (abs(length(debugPixel - dtid)) < 6)
        {
            DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + r.dir.xyz * min(r.dist, 2));
        }
        result.xyz *= success;
    }
    
    //lighted[dtid.xy] = result;
    //specularHitDistance[dtid.xy] = r.dist;
    
    RWTexture2D<float4> normal = ResourceDescriptorHeap[viewContext.normalIndex];
    normal[dtid.xy] = float4(cd.worldNorm, 1); // store the normal in full fp16 and not only 0-1 range
    
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