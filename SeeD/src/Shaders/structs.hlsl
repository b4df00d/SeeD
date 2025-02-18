#pragma once

#ifdef __cplusplus // bah c´est surtout pour par avoir ca dans le HLSL
#define align __declspec(align(16))
#else
#define align
#endif

// CAREFULL OF ALIGNEMENT !
// AVX 1 or 2 is not 16bytes aligned with hlsl++
// sse2 is. and thus can have a similar memory layout as hlsl compilation
// but prefere float4 or uint4 instead of float2 float3 because hlsl++ will still reserve a full float4 even for a float2

struct Options
{
    bool stopFrustumUpdate;
    bool stopBufferUpload;
    bool stepMotion;
} options;

namespace HLSL
{
    static const uint max_vertices = 64;
    static const uint max_triangles = 124;
#ifndef __cplusplus
    struct MSVert
    {
        float4 pos : SV_Position;
        float3 color : COLOR0;
    };
#endif
    
    static const uint invalidUINT = 4294967295;
    struct CommonResourcesIndices
    {
        uint meshesHeapIndex;
        uint meshCount;
        uint meshletsHeapIndex;
        uint meshletCount;
        uint meshletVerticesHeapIndex;
        uint meshletVertexCount;
        uint meshletTrianglesHeapIndex;
        uint meshletTriangleCount;
        uint verticesHeapIndex;
        uint vertexCount;
        uint camerasHeapIndex;
        uint cameraCount;
        uint lightsHeapIndex;
        uint lightCount;
        uint materialsHeapIndex;
        uint materialCount;
        uint instancesHeapIndex;
        uint instanceCount;
        uint instancesGPUHeapIndex; // only for instances created on GPU
        uint instanceGPUCount;
    };
    
    struct CullingContext
    {
        uint cameraIndex;
        uint lightsIndex;
        uint culledInstanceIndex;
        uint culledMeshletsIndex;
        uint instancesCounterIndex;
        uint meshletsCounterIndex;
        uint HZB;
        uint HZBMipCount;
        uint4 resolution;
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
    
    struct Meshlet
    {
        uint vertexOffset;
        uint triangleOffset;
        uint vertexCount;
        uint triangleCount;
        float4 boundingSphere;
    };

    struct Mesh
    {
        float4 boundingSphere;
        uint meshletOffset;
        uint meshletCount;
        uint vertexOffset;
        uint vertexCount;
        uint indexOffset;
        uint indexCount;
        uint pad[2];
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
    
    static const uint cullMeshletThreadCount = 128;
    struct InstanceCullingDispatch
    {
        uint instanceIndex;
        uint meshletIndex;
        
        // for D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH
        uint ThreadGroupCountX;
        uint ThreadGroupCountY;
        uint ThreadGroupCountZ;
    };
    
    struct MeshletDrawCall
    {
        uint instanceIndex;
        uint meshletIndex;
        
        // for D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH
        uint ThreadGroupCountX;
        uint ThreadGroupCountY;
        uint ThreadGroupCountZ;
    };
    
#ifndef __cplusplus
    typedef uint64_t D3D12_GPU_VIRTUAL_ADDRESS;
    struct D3D12_GPU_VIRTUAL_ADDRESS_RANGE
    {
        D3D12_GPU_VIRTUAL_ADDRESS StartAddress;
        uint64_t SizeInBytes;
    };
    struct D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE
    {
        D3D12_GPU_VIRTUAL_ADDRESS StartAddress;
        uint64_t SizeInBytes;
        uint64_t StrideInBytes;
    };
    struct D3D12_DISPATCH_RAYS_DESC
    {
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE RayGenerationShaderRecord;
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE MissShaderTable;
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE HitGroupTable;
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE CallableShaderTable;
        uint Width;
        uint Height;
        uint Depth;
    };
#endif
    
    struct RayDispatch
    {
        uint instanceIndex;
        uint meshletIndex;
        
        // for D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH
        D3D12_DISPATCH_RAYS_DESC rayDesc;
    };
    
    struct Camera
    {
        float4x4 view;
        float4x4 proj;
        float4x4 viewProj;
        float4 planes[6];
        float4 worldPos;
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
    
    // Hit information, aka ray payload
    // Note that the payload should be kept as small as possible,
    // and that its size must be declared in the corresponding
    // D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
    struct HitInfo
    {
        float3 color;
        float rayDepth;
        float3 currentPosition;
        uint rndseed;
        float3 normal;
        float tCurrent;
    };
    
    // Attributes output by the raytracing when hitting a surface,
    // here the barycentric coordinates
    struct Attributes
    {
        float2 bary;
    };
}