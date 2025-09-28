#include "structs.hlsl"

cbuffer CustomRT : register(b3)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Lighting

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void Lighting(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y) return;
    
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    RWTexture2D<float> specularHitDistance = ResourceDescriptorHeap[rtParameters.specularHitDistanceIndex];
    
    GBufferCameraData cd = GetGBufferCameraData(dtid.xy);
    SurfaceData s = GetSurfaceData(cd.pixel.xy);
    
    float hitDistance = 0;
    float3 direct = RESTIRLight(rtParameters.directReservoirIndex, cd, s, hitDistance);
    float3 indirect = RESTIRLight(rtParameters.giReservoirIndex, cd, s, hitDistance);
    
    float3 result = direct + indirect;
    
    lighted[dtid.xy] = float4(result / HLSL::brightnessClippingAdjust, 1); // scale down the result to avoid clipping the buffer format
    specularHitDistance[dtid.xy] = hitDistance;
    //lighted[dtid.xy] = float4(SampleProbes(rtParameters, cd.worldPos, s), 0);
#if 0
    if(dtid.x > viewContext.renderResolution.x * 0.5)
    {
        Texture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
        float3 ref = GI[dtid.xy] / HLSL::brightnessClippingAdjust;
        //ref /= r.dir_Wcount.w;
        lighted[dtid.xy] = float4(ref, 1);
        //lighted[dtid.xy] = float4(s.normal, 1);
    }
#endif
    
    if(cd.viewDist > 5000)
    {
        lighted[dtid.xy] = float4(Sky(cd.viewDir) * 0.25, 1);
        return;
    }
    
#if 1
    s.albedo = 1;
    
    float h = 0;
    float3 d = RESTIRLight(rtParameters.directReservoirIndex, cd, s, h);
    float3 i = RESTIRLight(rtParameters.giReservoirIndex, cd, s, h);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    GI[dtid.xy] = d + i;
#endif
    
#if 0
    int2 debugPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
    bool inRange = abs(length(debugPixel - int2(dtid.xy))) < 2.5;
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    HLSL::GIReservoir rid = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    RWStructuredBuffer<HLSL::GIReservoirCompressed> directReservoir = ResourceDescriptorHeap[rtParameters.directReservoirIndex];
    HLSL::GIReservoir rd = UnpackGIReservoir(directReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    if(inRange)
    {
        float3 endInDir = rid.dir;
        DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + endInDir);
        float3 endDir = rd.dir;
        DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + endDir);
        //lighted[dtid.xy] = float4(1, 0, 0, 1);
    }
#endif
}
