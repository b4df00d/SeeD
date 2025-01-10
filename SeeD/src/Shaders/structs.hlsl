#pragma once

#ifdef __cplusplus // bah c´est surtout pour par avoir ca dans le HLSL
#define align __declspec(align(16))
#else
#define align
#endif

// CAREFULL OF ALIGNEMENT !
// AVX 1 or 2 is not 16bytes aligned with hlsl++
// sse2 is. and thus can have a similar memory layout as hlsl compilation

namespace HLSL
{
    struct CommonResourcesIndices
    {
        uint meshesHeapIndex;
        uint meshletsHeapIndex;
        uint meshletVerticesHeapIndex;
        uint meshletTrianglesHeapIndex;
        uint verticesHeapIndex;
        uint camerasHeapIndex;
        uint lightsHeapIndex;
        uint materialsHeapIndex;
        uint instancesHeapIndex;
        uint instancesGPUHeapIndex; // only for instances created on GPU
    };
    
    struct Shader
    {
        uint id;
    };
    
    // must be similar to MeshLoader::Vertex
    struct Vertex
    {
        float3 pos;
        float3 normal;
        float2 uv;
    };
    
    // must be similar to MeshLoader::Triangles (unsigned char)
    struct triangleIndices
    {
        uint a, b, c;
    };
    
    // must be similar to MeshLoader::Mehslet
    struct Meshlet
    {
        uint vertexOffset;
        uint triangleOffset;
        uint vertexCount;
        uint triangleCount;
    };

    struct Mesh
    {
        uint meshletOffset;
        uint meshletCount;
    };
    
    struct Texture
    {
        uint index;
    };
    
    struct Material
    {
        uint shaderIndex;
        float parameters[15];
        Texture textures[16];
    };
    
    struct Instance
    {
        float4x4 worldMatrix;
        uint meshIndex;
        uint materialIndex;
        uint pad[2];
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