#include "structs.hlsl"

cbuffer CustomRT : register(b3)
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

//https://www.desmos.com/calculator/gslcdxvipg
float GranTurismoTonemapper(float x)
{
    float P = ppParameters.P;
    float a = ppParameters.a;
    float m = ppParameters.m;
    float l = ppParameters.l;
    float c = ppParameters.c;
    float b = ppParameters.b;
    /*
    float P = 1;
    float a = 1;
    float m = 0.5;
    float l = 0.5;
    float c = 0.5;
    float b = 0.0;
    */
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

float3 Saturation(float3 input, float strength)
{
    const float3 LuminanceWeights = float3(0.299, 0.587, 0.114);
    float luminance = dot(input, LuminanceWeights);
    return lerp(luminance, input, strength);
}

// ACES tone mapping curve fit to go from HDR to LDR
//https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void PostProcess(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if (dtid.x > viewContext.displayResolution.x || dtid.y > viewContext.displayResolution.y)
        return;
    
    uint2 renderPixel = dtid.xy * viewContext.displayResolution.zw * viewContext.renderResolution.xy;
    GBufferCameraData cd = GetGBufferCameraData(renderPixel.xy);
    
    uint2 inputPixel = dtid.xy;
    if(ppParameters.inputIsFullResolution == 0)
        inputPixel = dtid.xy * viewContext.displayResolution.zw * viewContext.renderResolution.xy;
    
    Texture2D<float4> lighted = ResourceDescriptorHeap[ppParameters.lightedIndex];
    float4 HDR = float4(lighted[inputPixel.xy].xyz * HLSL::brightnessClippingAdjust, 1);
    HDR -= ppParameters.expoAdd;
    HDR *= ppParameters.expoMul;
    HDR += ppParameters.expoAdd;
    
    //HDR = lerp(HDR, 0.5, saturate(1-exp(-cd.viewDist * 0.0025)));
    
#if 1
    float r = GranTurismoTonemapper(HDR.r);
    float g = GranTurismoTonemapper(HDR.g);
    float b = GranTurismoTonemapper(HDR.b);
    float4 SDR = float4(r, g, b, HDR.a);
#else
    float4 SDR = float4(ACESFilm(HDR.xyz), HDR.w);
#endif
    
    //if(any(HDR.xyz>=2.0)) SDR = float4(1, 0, 0, 0);
    
    RWTexture2D<float4> postProcessed = ResourceDescriptorHeap[ppParameters.postProcessedIndex];
    postProcessed[dtid.xy] = SDR; // write back in the albedo becasue it has the same format as backbuffer and we'll copy it just after this compute shader
    
    /*
    if(dtid.x > viewContext.displayResolution.x * 0.5)
    {
        //postProcessed[dtid.xy] = lighted[renderPixel.xy]; // write back in the albedo becasue it has the same format as backbuffer and we'll copy it just after this compute shader
        return;
    }
    */
    
    /*
    Texture2D<float2> motionT = ResourceDescriptorHeap[viewContext.motionIndex];
    float2 motion = motionT[dtid.xy];
    motion *= 0.1;
    albedo[dtid.xy].xy += abs(motion.xy);
    */
    
   //postProcessed[dtid.xy] = max(cd.viewDistDiff-0.1, 0);
    
   //postProcessed[dtid.xy].xyz = cd.worldPos.xyz;
}
