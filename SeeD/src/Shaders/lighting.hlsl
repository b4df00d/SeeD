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
    
    float3 direct = ComputeLight(light, shadows[dtid.xy], s, cd.viewDir);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];   
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
    HLSL::Light indirectLight;
    indirectLight.pos = float4(r.hit_Wsum.xyz, 0);
    indirectLight.dir = float4(-r.dir_Wcount.xyz, 0);
    indirectLight.color.xyz = r.color_W.xyz / r.color_W.w * (r.hit_Wsum.w / r.dir_Wcount.w);
    indirectLight.range = 1;
    indirectLight.angle = 1;
    float3 indirect = ComputeLight(indirectLight, 1, s, cd.viewDir);
    
    float3 result = direct + indirect;
    //result = direct;
    //result = indirect;
    
    if(cd.viewDist > 5000)
        result = Sky(cd.viewDir);
    
    lighted[dtid.xy] = float4(result / HLSL::brightnessClippingAdjust, 1); // scale down the result to avoid clipping the buffer format
    //lighted[dtid.xy] = float4(SampleProbes(rtParameters, cd.worldPos, s), 0) * 0.01;
#if 1
    if(dtid.x > viewContext.renderResolution.x * 0.5)
    {
        Texture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
        float3 ref = GI[dtid.xy] / HLSL::brightnessClippingAdjust;
        //ref /= r.dir_Wcount.w;
        lighted[dtid.xy] = float4(ref, 1);
    }
#endif
}
