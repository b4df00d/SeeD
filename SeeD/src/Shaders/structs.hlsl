#pragma once

namespace HLSL
{
    struct Shader
    {
        uint id;
    };
    
    struct Vertex
    {
        float3 pos;
        float3 normal;
        float2 uv;
    };
    
    struct Mesh
    {
        uint meshOffset;
        uint meshletCount;
    };
    
    struct Texture
    {
        uint index;
    };
    
    struct Material
    {
        Shader shader;
        float parameters[15];
        Texture textures[16];
    };
    
    struct Instance
    {
        uint meshIndex;
        uint materialIndex;
        float4x4 worldMatrix;
    };
    
    struct Camera
    {
        float4x4 viewProj;
    };

    struct Light
    {
        float3 pos;
        float3 dir;
        float3 color;
        float range;
        float angle;
    };
    
    struct DrawCall
    {
        float4x4 world;
        uint meshIndex;
        uint materialIndex;
    };
    
    struct Globals
    {
        uint2 resolution;
    };
    
#ifndef __cplusplus
    struct MSVert
    {
        float4 pos : SV_Position;
        float3 color : COLOR0;
    };
#endif
}