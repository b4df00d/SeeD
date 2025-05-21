#include "structs.hlsl"

cbuffer CustomRT : register(b2)
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
    if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y)
        return;
    
    Texture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    
    //Should not have any preference on shading a specific light here (the sun)... let the raytracing handle any light stuff ?
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    
    GBufferCameraData cd = GetGBufferCameraData(dtid.xy);
    SurfaceData s = GetSurfaceData(dtid.xy);
    
    float3 direct = BRDF(s, cd.viewDir, light.dir.xyz, shadows[dtid.xy] * light.color.xyz);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];   
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    float3 indirect = BRDF(s, cd.viewDir, r.dir_Wcount.xyz, r.color_W.xyz * (r.hit_Wsum.w / r.dir_Wcount.w), 0);
    //indirect = r.color_W.xyz * (r.hit_Wsum.w / r.dir_Wcount.w);
    //indirect = r.dir_Wcount.xyz;
#if 0
    Texture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    indirect *= 0.1;
    indirect += GI[dtid.xy].xyz;
#endif
    
    float3 result = direct + indirect;
    //result = direct;
    //result = indirect;
    //result = SampleProbes(rtParameters, cd.worldPos, cd.worldNorm);
    
    if(cd.viewDist > 5000)
        result = Sky(cd.viewDir);
    
    lighted[dtid.xy] = float4(result / HLSL::brightnessClippingAdjust, 1); // scale down the result to avoid clipping the buffer format
    //lighted[dtid.xy] = float4(brdf, 1);
    //lighted[dtid.xy] = shadows[dtid.xy];
}
