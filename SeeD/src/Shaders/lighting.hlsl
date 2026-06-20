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
        lighted[dtid.xy] = float4(Sky(cd.viewDir), 1);
        specularHitDistance[dtid.xy] = 10000 * 100;
        return;
    }
    SurfaceData s = GetSurfaceData(dtid.xy);
    
    uint seed = initRand(dtid.xy);
    uint poissonDiskCount = clamp(rtParameters.spacialSampleCount, 1u, 64u); // tweakable neighbour count
    uint poissonIndexRng = nextRand(seed) * (64 - poissonDiskCount);
    
    RaytracingAccelerationStructure AccelerationStructure = ResourceDescriptorHeap[rtParameters.BVH];
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    uint resW = viewContext.renderResolution.x;
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * resW]);

    // ---- Spatial ReSTIR reuse (pairwise MIS) ---------------------------------
    // Combine this pixel's (temporal) reservoir = the "canonical" sample with a few
    // screen-space neighbours. Each neighbour is MIS-weighted against the canonical
    // so the reconnection Jacobian appears in BOTH the resampling weight and the MIS
    // denominator -> unbiased even at Wcount==1 (no temporal). The plain 1/M combine
    // it replaces let J brighten freely because E[J] >= 1 (reciprocal-symmetric shift).
    //
    // CRITICAL: each neighbour's MIS weight carries a 1/M factor (M = #neighbours), and
    // the canonical takes the remainder m0 = 1 - sum(m_i). Without the 1/M the weights
    // do not sum to 1 and the image brightens linearly with spacialSampleCount.
    //
    // The target fn here is luminance (geometry-free), so the per-pair target factors
    // cancel and the weights collapse to functions of confidence (Wcount) and J:
    //   neighbour i :  m_i = (1/M) * cI / (cI + cC*J)     w_i = m_i * J * Wsum_i / cI
    //   canonical   :  m0  = 1 - sum(m_i)                 w0  = m0 * Wsum_c / cC
    // Because 1/M is unknown until the loop ends, we WRS-pick a neighbour "champion"
    // with the un-normalised weights, then do a final draw champion-vs-canonical
    // (hierarchical WRS == flat WRS over all candidates).
    //
    // Neighbour reservoirs were finalised by Pass 1 (raytracing2.hlsl) and are
    // safe to read thanks to the giReservoir UAV barrier before this dispatch.
    float radius = rtParameters.spacialRadius;
    if(radius > 0)
    {
        float cC    = max(r.Wcount, 1.0f);          // canonical confidence (floored so an empty
                                                    // centre can't N*-brighten via cC==0)
        float pHatC = r.W;                          // canonical target (Luminance of its colour)
        HLSL::GIReservoir champ = (HLSL::GIReservoir)0;  // selected neighbour (champion)
        float champWsumPrime = 0.0f;                // running un-normalised weight sum (= M * sum w_i)
        float sumMiTimesM    = 0.0f;                // sum cI/(cI+cC*J) = M * sum(m_i)
        uint  validNeighbors = 0;
        for (uint i = 0; i < poissonDiskCount; i++)
        {
            int2 np = dtid.xy + poissonDisk[i + poissonIndexRng] * radius * (i / float(poissonDiskCount));
            if (np.x < 0 || np.y < 0) continue;
            if (np.x >= (int)resW || np.y >= (int)viewContext.renderResolution.y) continue;
            if (np.x == dtid.x && np.y == dtid.y) continue;  // skip self (i==0 lands on the centre pixel)

            GBufferCameraData ncd = GetGBufferCameraData(np.xy);
            // Receiver-similarity gates keep the (BRDF-not-re-evaluated) reuse valid.
            if (abs(cd.viewDist - ncd.viewDist) > 0.2) continue;
            if (dot(cd.worldNorm, ncd.worldNorm) < 0.9) continue;

            HLSL::GIReservoir rn = UnpackGIReservoir(giReservoir[np.x + np.y * resW]);
            if (rn.Wcount <= 0 || rn.W <= 0) continue;

            // Reconnect the neighbour's sample point (built from ITS origin) to our receiver.
            float3 samplePos = ncd.offsetedWorldPos + rn.dir * rn.dist;
            float3 toSample  = samplePos - cd.offsetedWorldPos;
            float  newDist   = length(toSample);
            float3 newDir    = toSample / max(newDist, 1e-6f);

            // ReSTIR GI reconnection Jacobian (neighbour -> this receiver):
            //   J = (cosR / dR^2) / (cosQ / dQ^2) = (cosR * dQ^2) / (cosQ * dR^2)
            float dQ   = max(rn.dist, 1e-6f);
            float dR   = max(newDist, 1e-6f);
            float cosQ = abs(dot(normalize(ncd.offsetedWorldPos - samplePos), rn.hitNormal));
            float cosR = abs(dot(normalize(cd.offsetedWorldPos  - samplePos), rn.hitNormal));
            if (cosQ <= 1e-3f) continue;                          // grazing donor
            float J = (cosR * dQ * dQ) / max(cosQ * dR * dR, 1e-6f);
            if (J <= 0.0f || J > 10.0f) continue;                 // firefly/variance guard (no longer a bias fix)

            float cI        = rn.Wcount;                          // neighbour confidence
            float denomPair = max(cI + cC * J, 1e-6f);            // balance-heuristic denominator for this pair

            // (M * m_i) = cI/(cI+cC*J);  (M * w_i) = J * Wsum_i / (cI+cC*J).
            sumMiTimesM += cI / denomPair;
            float wPrime = J * rn.Wsum / denomPair;

            validNeighbors++;
            champWsumPrime += wPrime;
            if (wPrime > 0.0f && nextRand(seed) * champWsumPrime < wPrime)
            {
                champ.color     = rn.color;
                champ.W         = Luminance(rn.color);   // == pHat at receiver (geometry-free target)
                champ.dir       = newDir;     // reconnected from THIS receiver
                champ.dist      = newDist;
                champ.hitNormal = rn.hitNormal;
            }
        }

        // Finalise the MIS weights now that M is known, then draw champion vs canonical.
        float invM   = (validNeighbors > 0) ? 1.0f / (float)validNeighbors : 0.0f;
        float wNb    = champWsumPrime * invM;            // total neighbour weight = sum w_i
        float m0     = saturate(1.0f - sumMiTimesM * invM);   // canonical MIS weight = 1 - sum(m_i)
        float w0     = m0 * r.Wsum / cC;                 // canonical resampling weight (0 if centre empty)

        HLSL::GIReservoir combined = champ;   // champ is zero-init when no neighbour was picked
        combined.Wsum   = wNb + w0;
        combined.Wcount = cC + (float)validNeighbors;    // bookkeeping only; resolve pins Wcount = 1
        if (w0 > 0.0f && nextRand(seed) * combined.Wsum < w0)
        {
            combined.color     = r.color;
            combined.W         = pHatC;
            combined.dir       = r.dir;
            combined.dist      = r.dist;
            combined.hitNormal = r.hitNormal;
        }

        // Visibility test of the finally-selected reconnection; on occlusion keep the
        // central reservoir (rejects indirect light leaking through geometry).
        if (validNeighbors > 0 && combined.W > 0.0f)
        {
            RayDesc ray;
            ray.Origin    = cd.offsetedWorldPos;
            ray.Direction = combined.dir;
            ray.TMin      = 0.0f;
            ray.TMax      = combined.dist;

            ShadowRayPayload payload;
            payload.visibility = float3(1.0f, 1.0f, 1.0f);
            TraceRay(AccelerationStructure, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, SHADOW_RAY_INDEX, 0, SHADOW_RAY_INDEX, ray, payload);

            if (any(payload.visibility > 0))   // unoccluded -> accept the spatially-combined reservoir
            {
                // The pairwise estimator's UCW is already Wsum / pHat(Y) (MIS weights sum to 1,
                // so there is NO 1/M factor). Pin Wcount = 1 so the shared resolve below
                // (Wsum / (Wcount * W)) evaluates to exactly that and stays unbiased.
                r = combined;
                r.Wcount = 1.0f;
            }
        }
    }

    // ---- Resolve indirect from the reservoir and composite onto direct light --
    // UCW = Wsum / (M * pHat_selected); indirect radiance estimate = color * UCW.
    // For the temporal-only path M = r.Wcount; for the spatial pairwise-MIS path the
    // MIS weights already sum to 1 (no 1/M), so it pinned r.Wcount = 1 above to reuse
    // this same expression. The reservoir stores UNSCALED path radiance, so apply
    // SHARCRadianceScale here to match the direct light Pass 1 scaled in ResolveSampleData.
    float ucw = r.Wsum / max(r.Wcount * r.W, 1e-5f);
    float3 indirect = r.color * ucw * rtParameters.SHARCRadianceScale;
    if (!editorContext.GIBounces && !editorContext.GIAlbedo && !editorContext.GINormals)
        lighted[dtid.xy] += float4(indirect, 0.0f);



    
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