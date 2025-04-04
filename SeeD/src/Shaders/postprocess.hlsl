#include "structs.hlsl"

cbuffer CustomRT : register(b2)
{
    HLSL::PostProcessParameters ppParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute PostProcess


static const float e = 2.71828;

float W_f(float x, float e0, float e1)
{
    if (x <= e0)
        return 0;
    if (x >= e1)
        return 1;
    float a = (x - e0) / (e1 - e0);
    return a * a * (3 - 2 * a);
}
float H_f(float x, float e0, float e1)
{
    if (x <= e0)
        return 0;
    if (x >= e1)
        return 1;
    return (x - e0) / (e1 - e0);
}

float GranTurismoTonemapper(float x)
{
    float P = 1;
    float a = 1;
    float m = 0.22;
    float l = 0.4;
    float c = 1.33;
    float b = 0;
    float l0 = (P - m) * l / a;
    float L0 = m - m / a;
    float L1 = m + (1 - m) / a;
    float L_x = m + a * (x - m);
    float T_x = m * pow(x / m, c) + b;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = a * P / (P - S1);
    float S_x = P - (P - S1) * pow(e, -(C2 * (x - S0) / P));
    float w0_x = 1 - W_f(x, 0, m);
    float w2_x = H_f(x, m + l0, m + l0);
    float w1_x = 1 - w0_x - w2_x;
    float f_x = T_x * w0_x + L_x * w1_x + S_x * w2_x;
    return f_x;
}


[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void PostProcess(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if (dtid.x > ppParameters.resolution.x || dtid.y > ppParameters.resolution.y)
        return;
    
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[ppParameters.lightedIndex];
    RWTexture2D<float4> albedo = ResourceDescriptorHeap[ppParameters.albedoIndex];
    
    float4 HDR = lighted[dtid.xy];
    
    float r = GranTurismoTonemapper(HDR.r);
    float g = GranTurismoTonemapper(HDR.g);
    float b = GranTurismoTonemapper(HDR.b);
    float4 SDR = float4(r, g, b, HDR.a);
    
    albedo[dtid.xy] = SDR; // write back in the albedo becasue it has the same format as backbuffer and we'll copy it just after this compute shader
}
