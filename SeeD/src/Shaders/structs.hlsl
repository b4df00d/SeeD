#pragma once

namespace HLSL
{
    struct Shader
    {
        uint id;
    };
    
    struct Mesh
    {
        
    };
    
    struct Texture
    {
        uint index;
    };
    
    struct Camera
    {
        float4x4 viewProj;
    };
    
    struct Material
    {
        Shader shader;
        float parameters[15];
        Texture textures[16];
    };

    struct Light
    {
        float3 pos;
        float3 dir;
        float3 color;
        float range;
        float angle;
    };
    
#ifndef __cplusplus
    struct MSVert
    {
        float4 pos : SV_Position;
        float3 color : COLOR0;
    };
#endif
}