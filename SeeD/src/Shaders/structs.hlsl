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

#include "sphericalharmonics.hlsl"

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
        float4 previousPos : TEXCOORD0;
        float3 normal : NORMAL;
        float3 color : COLOR0;
        float2 uv : TEXCOORD1;
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
        float4 resolution; //x, y, 1/x, 1/y
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
        uint motionIndex;
        uint depthIndex;
        uint HZB;
        uint HZBMipCount;
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
        uint pad1;
        uint pad2;
    };
    
    struct Material
    {
        uint textures[16];
        float parameters[15];
        uint shaderIndex;
    };
    
    struct Instance
    {
        float4x4 current; // FFS hlsl++ does store 4x3 and 4x3 in the same way ... BS ! TODO : make the 4x3 packing work
        float4x4 previous;
        uint meshIndex;
        uint materialIndex;
        uint pad1;
        uint pad2;
        
        float4x4 unpack(float4x4 mat)
        {
            return mat;
        }
        
        float4x4 pack(float4x4 mat)
        {
            return mat;
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
        // current
        float4x4 view;
        float4x4 proj;
        float4x4 viewProj;
        float4x4 viewProj_inv;
        float4 planes[6];
        float4 worldPos;
        
        //previous
        float4x4 previousViewProj;
        float4x4 previousViewProj_inv;
        float4 previousWorldPos;
    };

    struct Light
    {
        float4 pos;
        float4 dir;
        float4 color;
        float range;
        float angle;
        uint pad1;
        uint pad2;
    };
    
    struct PostProcessParameters
    {
        float4 resolution; //x, y, 1/x, 1/y
        uint lightedIndex;
        uint backBufferIndex;
        //tonemap
        float P;
        float a;
        float m;
        float l;
        float c;
        float b;
        float expoAdd;
        float expoMul;
    };
    static const float brightnessClippingAdjust = 4;
    
    struct ProbeData
    {
        HLSL::SHProbe sh;
        float4 position;
    };
    
    struct ProbeGrid
    {
        float4 probesBBMin;
        float4 probesBBMax;
        uint4 probesResolution;
        uint4 probesAddressOffset;
        uint probesIndex;
        uint probesSamplesPerFrame;
        uint pad1; // dont use a uint[2] it does some more alignement and thus put pad between probesSamplesPerFrame and our pad
        uint pad2;
    };
    
    // ----------------- RT stuff ------------------
    static const uint maxRTDepth = 2;
    struct RTParameters
    {
        float4 resolution; //x, y, 1/x, 1/y
        
        uint frame;
        
        uint BVH;
        uint giIndex;
        uint giReservoirIndex;
        uint previousgiReservoirIndex;
        uint shadowsIndex;
        uint lightedIndex;
        
        uint passNumber;
        
        uint probeToCompute;
        ProbeGrid probes[3];
    };
    
    // Hit information, aka ray payload
    // Note that the payload should be kept as small as possible,
    // and that its size must be declared in the corresponding
    // D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
    struct HitInfo
    {
        float3 color;
        float hitDistance;
        uint rayDepth; //pack depth and seed in 1 uint
        uint rndseed;
    };
    
    // Attributes output by the raytracing when hitting a surface,
    // here the barycentric coordinates
    struct Attributes
    {
        float2 bary;
    };
    
    struct GIReservoir
    {
        float4 color_W;
        float4 dir_Wcount;
        float4 pos_Wsum;
    };
    
    struct GIReservoirCompressed
    {
        float4 pos_Wsum;
        uint dir;
        uint color;
        uint Wcount_W;
    };
 /*
not packed 220fps

packed color only = 270fps

packed : 
        float4 pos_Wsum;
        uint dir;
        uint color;
        uint Wcount_W;
        = 318fps
*/
    // ----------------- End RT stuff ------------------
}
#endif // __STRUCTS__