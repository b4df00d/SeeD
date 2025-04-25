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
    Texture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    Texture2D<float> shadows = ResourceDescriptorHeap[rtParameters.shadowsIndex];
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    Texture2D<float4> albedo = ResourceDescriptorHeap[cullingContext.albedoIndex];
    
    //Should not have any preference on shading a specific light here (the sun)... let the raytracing handle any light stuff ?
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    HLSL::Light light = lights[0];
    
    GBufferCameraData cd = GetGBufferCameraData(dtid.xy);
    
    float3 indirect = GI[dtid.xy].xyz;
    float3 direct = shadows[dtid.xy] * light.color.xyz;
    
    SurfaceData s;
    s.albedo = albedo[dtid.xy].xyz;
    s.roughness = 0.99;
    s.normal = cd.worldNorm;
    s.metalness = 0.1;
    
    
    float3 brdf = BRDF(s, cd.viewDir, light.dir.xyz, direct);
    float3 ambient = indirect * s.albedo;
    //ambient = (dot(s.normal, float3(0, 1, 0)) * 0.5 + 0.5) * 0.5;
    
    float3 result = brdf + ambient;
    //result = direct;
    //result = indirect;
#if false
    float3 samplePos = cd.worldPos;
    
    uint probeGridIndex = 0;
    HLSL::ProbeGrid probes = rtParameters.probes[probeGridIndex];
    while(probeGridIndex < 3 && (any(samplePos.xyz < probes.probesBBMin.xyz) || any(samplePos.xyz > probes.probesBBMax.xyz)))
    {
        probeGridIndex++;
        probes = rtParameters.probes[probeGridIndex];
    }
    
    float3 cellSize = float3(probes.probesBBMax.xyz - probes.probesBBMin.xyz) / float3(probes.probesResolution.xyz);
    int3 launchIndex = (samplePos - probes.probesBBMin.xyz) / (probes.probesBBMax.xyz - probes.probesBBMin.xyz) * probes.probesResolution.xyz;
    //launchIndex = min(max(0, launchIndex), probes.probesResolution.xyz);
    uint3 wrapIndex = ModulusI(launchIndex.xyz + probes.probesAddressOffset.xyz, probes.probesResolution.xyz);
    uint probeIndex = wrapIndex.x + wrapIndex.y * probes.probesResolution.x + wrapIndex.z * (probes.probesResolution.x * probes.probesResolution.y);
    StructuredBuffer<HLSL::ProbeData> probesBuffer = ResourceDescriptorHeap[probes.probesIndex];
    HLSL::ProbeData probe = probesBuffer[probeIndex];
    result = max(0.0f, shUnproject(probe.sh.R, probe.sh.G, probe.sh.B, s.normal)); // A "max" is usually recomended to avoid negative values (can happen with SH)
    //result = RandUINT(probeIndex);
#endif
    
    lighted[dtid.xy] = float4(result / HLSL::brightnessClippingAdjust, 1); // scale down the result to avoid clipping the buffer format
}
