#include "structs.hlsl"

cbuffer CustomRT : register(b3)
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

static int2 patternA[4] =
{
    int2(-1, 0),
    int2(1, 0),
    int2(0, -1),
    int2(0, 1)
};

[shader("raygeneration")]
void RayGen()
{
    uint2 dtid = DispatchRaysIndex().xy;
    //if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y) return;
    
    GBufferCameraData cd = GetGBufferCameraData(dtid);
    if (cd.reverseZ <= 0)
        return;
    
    uint seed = initRand(dtid.xy);
    
    SurfaceData s = GetSurfaceData(dtid.xy);
    
    RESTIRRay indirectRay;
    indirectRay.Origin = cd.offsetedWorldPos;
    indirectRay.Direction = normalize(lerp(reflect(cd.viewDir, s.normal), getCosHemisphereSample(seed, s.normal), s.roughness));
    indirectRay = IndirectLight(rtParameters, s, indirectRay, 0, seed);
    RESTIR(rtParameters, indirectRay, rtParameters.previousgiReservoirIndex, rtParameters.giReservoirIndex, cd, seed);
    
    RESTIRRay directRay;
    directRay.Origin = cd.offsetedWorldPos;
    directRay = DirectLight(rtParameters, directRay, 0, seed);
    RESTIR(rtParameters, directRay, rtParameters.previousDirectReservoirIndex, rtParameters.directReservoirIndex, cd, seed);
    
    if (editorContext.rays) // daw ray
    {
        RWStructuredBuffer<HLSL:: GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
        HLSL::GIReservoir rd = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
        uint2 debugPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
        if (abs(length(debugPixel - dtid)) < 6)
        {
            DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + rd.dir.xyz * min(rd.dist, 2));
        }
    }
    /*
#define REFERENCE
#ifdef REFERENCE
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    float3 direct = ComputeLight(light, 1, s, cd.viewDir);
    HLSL::Light indirectLight;
    indirectLight.pos = float4(0,0,0, 0);
    indirectLight.dir = float4(normalize(cd.worldPos - r.hit_Wsum.xyz), 0);
    indirectLight.color.xyz = bounceLight.xyz;
    indirectLight.range = 1;
    indirectLight.angle = 1;
    float3 indirect = ComputeLight(indirectLight, 1, s, cd.viewDir);
    float3 result = direct + indirect;
    //result *= 10;
        
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    if (viewContext.frameNumber == 0) GI[dtid] = result;
    GI[dtid] = (GI[dtid] * (r.dir_Wcount.w-1) + result) / r.dir_Wcount.w;
#endif
    */
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
    CommonMiss(WorldRayOrigin(), WorldRayDirection(), RayTCurrent(), payload);
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    CommonHit(rtParameters, InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.bary, WorldRayOrigin(),  WorldRayDirection(), RayTCurrent(), 254/*ReportHit()*/, payload);
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
    payload.hitDistance = RayTCurrent();
}