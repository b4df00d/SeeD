#include "structs.hlsl"

cbuffer CustomRT : register(b3)
{
    HLSL::PostProcessParameters ppParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute PostProcess PostProcess

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

// Every tonemapper below maps linear scene HDR to linear display [0,1]; the sRGB encode is done once
// at the end of PostProcess.

// ACES tone mapping curve fit to go from HDR to LDR
//https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ACESFilm(float3 x)
{
    x *= 0.6f; // the fit bakes in the ACES RRT's 1/0.6 exposure; undo it so exposure 1 matches the other curves
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

// AgX (Troy Sobotka), Blender's default view transform since 4.0.
// Minimal fit by Benjamin Wrensch: https://iolite-engine.com/blog_posts/minimal_agx_implementation
// Matrices are the GLSL column-major ones written as HLSL rows, hence mul(v, M).
float3 AgXContrastApprox(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
float3 AgX(float3 color, bool punchy)
{
    const float3x3 agxMat = float3x3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 agxMatInv = float3x3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    color = mul(max(color, 1e-10), agxMat); // log2 of <= 0 would be -inf / NaN
    color = clamp(log2(color), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    color = AgXContrastApprox(color);

    if (punchy) // ASC CDL "Punchy" look, as in Blender
    {
        const float3 lw = float3(0.2126, 0.7152, 0.0722);
        float luma = dot(color, lw);
        color = pow(max(color, 0), 1.35);
        color = luma + 1.4 * (color - luma);
    }

    color = mul(color, agxMatInv);
    return pow(saturate(color), 2.2); // AgX's output is 2.2-encoded; back to display linear
}

// Khronos PBR Neutral: keeps base colors 1:1 below the highlights, then compresses toward white.
// https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/README.md
float3 PBRNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return color;

    const float d = 1. - startCompression;
    float newPeak = 1. - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
    return lerp(color, newPeak, g);
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
    
    RWTexture2D<float4> postProcessed = ResourceDescriptorHeap[ppParameters.postProcessedIndex];
    // lightedIndex is a UAV slot either way (lighted.uav.offset or, for DLSS/DLSS-RR, upscaled.uav.offset
    // -- Renderer.h PostProcess::Render -- both live permanently in UNORDERED_ACCESS), read-only here.
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[ppParameters.lightedIndex];
    float4 HDR = float4(lighted[inputPixel.xy].xyz * HLSL::brightnessClippingAdjust, 1);
    
    if (editorContext.debugFlags & (HLSL::EditorDebugGIBounces | HLSL::EditorDebugGINormals | HLSL::EditorDebugAlbedo | HLSL::EditorDebugNormals | HLSL::EditorDebugClusters | HLSL::EditorDebugTriangles))
    {
        // albedo is linear since the texture decode: encode it so the view shows the authored colors
        if (editorContext.debugFlags & HLSL::EditorDebugAlbedo)
            HDR.xyz = LinearToSRGB(HDR.xyz);
        postProcessed[dtid.xy] = HDR;
        return;
    }

    HDR *= ppParameters.expoMul;
    HDR += ppParameters.expoAdd;

    float3 SDR;
    switch (ppParameters.tonemapper)
    {
    case HLSL::TonemapperGT:
        SDR = float3(GranTurismoTonemapper(HDR.r), GranTurismoTonemapper(HDR.g), GranTurismoTonemapper(HDR.b));
        break;
    case HLSL::TonemapperACES:
        SDR = ACESFilm(HDR.xyz);
        break;
    case HLSL::TonemapperAgX:
        SDR = AgX(HDR.xyz, false);
        break;
    case HLSL::TonemapperAgXPunchy:
        SDR = AgX(HDR.xyz, true);
        break;
    case HLSL::TonemapperPBRNeutral:
        SDR = PBRNeutral(HDR.xyz);
        break;
    default:
        SDR = HDR.xyz;
        break;
    }

    // the backbuffer (and postProcessed, copied into it) is plain R8G8B8A8_UNORM: encode by hand
    postProcessed[dtid.xy] = float4(LinearToSRGB(SDR), HDR.a); // write back in the albedo becasue it has the same format as backbuffer and we'll copy it just after this compute shader
    
    
    /*
    float blend = 1.0-saturate((cd.viewDistDiff - 0.01) * 100);
    postProcessed[dtid.xy] = blend; // write back in the albedo becasue it has the same format as backbuffer and we'll copy it just after this compute shader
    */
    
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
