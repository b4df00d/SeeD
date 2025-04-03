#pragma once
#ifndef __STRUCTS__
#define __STRUCTS__

#ifdef __cplusplus // bah c´est surtout pour par avoir ca dans le HLSL
#define SeeDalign __declspec(align(16))
#else
#define SeeDalign
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
    bool shaderReload;
} options;

namespace HLSL
{
    static const uint max_vertices = 64;
    static const uint max_triangles = 124;
#ifndef __cplusplus
    struct MSVert
    {
        float4 pos : SV_Position;
        float3 normal : NORMAL;
        float3 color : COLOR0;
        float2 uv : TEXCOORD0;
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
        uint indicesHeapIndex;
        uint indexCount;
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
        uint frameNumber;
        uint frameTime;
        uint cameraIndex;
        uint lightsIndex;
        uint culledInstanceIndex;
        uint culledMeshletsIndex;
        uint instancesCounterIndex;
        uint meshletsCounterIndex;
        uint albedoIndex;
        uint normalIndex;
        uint depthIndex;
        uint HZB;
        uint HZBMipCount;
        float4 resolution;
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
    
    struct Material
    {
        uint textures[16];
        float parameters[15];
        uint shaderIndex;
    };
    
    struct Instance
    {
        //float4x4 worldMatrix;
        float4 matA;
        float4 matB;
        float4 matC;
        uint meshIndex;
        uint materialIndex;
        uint pad[2];
        
        float4x4 unpack()
        {
            return float4x4(matA.x, matB.x, matC.x, matA.w,
                            matA.y, matB.y, matC.y, matB.w,
                            matA.z, matB.z, matC.z, matC.w,
                            0, 0, 0, 1);
        }
        
        void pack(float4x4 mat)
        {
            matA.xyz = mat[0].xyz;
            matA.w = mat[3].x;
            
            matB.xyz = mat[1].xyz;
            matB.w = mat[3].y;
            
            matC.xyz = mat[2].xyz;
            matC.w = mat[3].z;
        }
    };
    
    static const uint cullMeshletThreadCount = 32;
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
        float4x4 viewProj_inv;
        float4 planes[6];
        float4 worldPos;
    };

    struct Light
    {
        float4 pos;
        float4 dir;
        float4 color;
        float range;
        float angle;
        uint pad[2];
    };
    
    struct Globals
    {
        float4 resolution; //x, y, 1/x, 1/y
        uint albedoIndex;
        uint normalIndex;
    };
    
    struct PostProcessParameters
    {
        float4 resolution; //x, y, 1/x, 1/y
        uint lightedIndex;
        uint albedoIndex;
        uint normalIndex;
        uint depthIndex;
        uint backBufferIndex;
    };
    
    
    // ----------------- RT stuff ------------------
    static const uint maxRTDepth = 3;
    struct RTParameters
    {
        float4 resolution; //x, y, 1/x, 1/y
        uint BVH;
        uint giIndex;
        uint shadowsIndex;
        uint restirIndex;
        uint lightedIndex;
    };
    
    // Hit information, aka ray payload
    // Note that the payload should be kept as small as possible,
    // and that its size must be declared in the corresponding
    // D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
    struct HitInfo
    {
        float3 color;
        uint rayDepth;
        uint rndseed;
        float hitDistance;
        //float tCurrent;
        //float3 currentPosition;
        //float3 normal;
    };
    
    // Attributes output by the raytracing when hitting a surface,
    // here the barycentric coordinates
    struct Attributes
    {
        float2 bary;
    };
    // ----------------- End RT stuff ------------------
}
#endif // __STRUCTS__