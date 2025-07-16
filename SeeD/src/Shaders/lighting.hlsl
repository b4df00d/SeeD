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
    
    GBufferCameraData cd = GetGBufferCameraData(dtid.xy);
    SurfaceData s = GetSurfaceData(cd.pixel.xy);
    
    float3 direct = RESTIRLight(rtParameters.directReservoirIndex, cd, s);
    float3 indirect = RESTIRLight(rtParameters.giReservoirIndex, cd, s);
    
    float3 result = direct + indirect;
    
    lighted[dtid.xy] = float4(result / HLSL::brightnessClippingAdjust, 1); // scale down the result to avoid clipping the buffer format
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
    
#if 0
        RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
        GI[dtid.xy] = cd.viewDistDiff;
#endif
    
    if(cd.viewDist > 5000)
        lighted[dtid.xy] = float4(Sky(cd.viewDir) * 0.25, 1);
    
    int2 debugPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
    bool inRange = abs(length(debugPixel - int2(dtid.xy))) < 2.5;
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
    HLSL::GIReservoir rid = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    RWStructuredBuffer<HLSL::GIReservoirCompressed> directReservoir = ResourceDescriptorHeap[rtParameters.directReservoirIndex];
    HLSL::GIReservoir rd = UnpackGIReservoir(directReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    if(inRange)
    {
        float3 endInDir = normalize(rid.hit_Wsum.xyz - cd.offsetedWorldPos);
        DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + endInDir);
        float3 endDir = normalize(rd.hit_Wsum.xyz - cd.offsetedWorldPos);
        DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + endDir);
        lighted[dtid.xy] = float4(1, 0, 0, 1);
    }
}
