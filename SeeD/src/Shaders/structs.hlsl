#pragma once
#ifndef __STRUCTS__
#define __STRUCTS__

#define GROUPED_CULLING 1
#define GROUPED_CULLING_THREADS 128
#define INSTANCE_CULLING_THREADS 128

#ifdef __cplusplus // bah c�est surtout pour par avoir ca dans le HLSL
#define SeeDalign __declspec(align(16))
#else
#define SeeDalign
#endif

// CAREFULL OF ALIGNEMENT !
// AVX 1 or 2 is not 16bytes aligned with hlsl++
// sse2 is. and thus can have a similar memory layout as hlsl compilation
// but prefere float4 or uint4 instead of float2 float3 because hlsl++ will still reserve a full float4 even for a float2

#include "sphericalharmonics.hlsl"

namespace HLSL
{
#ifdef __cplusplus // bah c�est surtout pour par avoir ca dans le HLSL
    #define CommonResourcesIndicesRegister 0
    #define ViewContextRegister 1
    #define EditorContextRegister 2
    #define Custom1Register 3
    #define Custom2Register 4
    #define InstanceIndexIndirectRegister 5
    #define meshletIndexIndirectRegister 6
#else
    #define CommonResourcesIndicesRegister b0
    #define ViewContextRegister b1
    #define EditorContextRegister b2
    #define Custom1Register b3
    #define Custom2Register b4
    #define InstanceIndexIndirectRegister b5
    #define meshletIndexIndirectRegister b6
#endif
    
    
    static const bool reverseZ = true;
    
    static const uint max_vertices = 64;
    static const uint max_triangles = 124;
    
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
    
    enum class Upscaling
    {
        none,
        taa,
        dlss,
        dlssd
    };
    
    struct ViewContext
    {
        float4 renderResolution; //x, y, 1/x, 1/y
        float4 displayResolution; //x, y, 1/x, 1/y
        int4 mousePixel;
        float4 jitter; // current.xy, previous.zw
        Upscaling upscaling;
        uint frameNumber;
        uint frameTime;
        uint cameraIndex;
        uint lightsIndex;
        uint instancesCulledArgsIndex;
#if GROUPED_CULLING
        uint meshletsToCullIndex;
#endif
        uint meshletsCulledArgsIndex;
        uint instancesCounterIndex;
        uint meshletsCounterIndex;
        uint albedoIndex;
        uint metalnessIndex;
        uint roughnessIndex;
        uint normalIndex;
        uint motionIndex;
        uint objectIDIndex;
        uint instanceIDIndex;
        uint depthIndex;
        uint reverseZ;
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
        float3 tangent;
        float3 binormal;
        float2 uv;
        float2 uv1;
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
        uint lodCount;
        uint storageIndex;
        uint vertexOffset;
        uint vertexCount;
        struct LOD
        {
            uint meshletOffset;
            uint meshletCount;
            uint indexOffset;
            uint indexCount;
        };
        LOD LODs[4];
    };
    
    static const uint MaterialTextureCount = 7;
    static const uint MaterialParametersCount = 24;
    struct Material
    {
        float parameters[MaterialParametersCount];
        uint textures[MaterialTextureCount];
        uint shaderIndex;
    };
    
    struct Instance
    {
        float4x4 current; // FFS hlsl++ does store 4x3 and 4x3 in the same way ... BS ! TODO : make the 4x3 packing work
        float4x4 previous;
        uint meshIndex;
        uint materialIndex;
        uint objectID; // map to entityBase
        uint instanceID;
        
        float3 GetPosition()
        {
            return float3(current[0][3], current[1][3], current[2][3]);
        }
        
        float GetScale()
        {
            return abs(max(max(length(current[0].xyz), length(current[1].xyz)), length(current[2].xyz)));
        }
        
        float4x4 unpack(float4x4 mat)
        {
            return mat;
        }
        
        float4x4 pack(float4x4 mat)
        {
            return mat;
        }
    };
    
    // Data structure to match the command signature used for ExecuteIndirect.
    #ifdef __cplusplus // bah c�est surtout pour par avoir ca dans le HLSL
    #else
    typedef uint2 D3D12_GPU_VIRTUAL_ADDRESS;
    struct D3D12_DRAW_ARGUMENTS 
    {
      uint VertexCountPerInstance;
      uint InstanceCount;
      uint StartVertexLocation;
      uint StartInstanceLocation;
    };
    struct D3D12_DRAW_INDEXED_ARGUMENTS
    {
	    uint IndexCountPerInstance;
	    uint InstanceCount;
	    uint StartIndexLocation;
	    int BaseVertexLocation;
	    uint StartInstanceLocation;
    };
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

    struct IndirectCommand
    {
	    uint index;
        //uint stuff;
	    //D3D12_GPU_VIRTUAL_ADDRESS cbv;
	    D3D12_DRAW_ARGUMENTS drawArguments;
    };
    
    #if GROUPED_CULLING
    struct GroupedCullingDispatch
    {
        uint instanceIndex;
        uint meshletIndex;
    };
    #endif
    
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
        float4x4 view_inv;
        float4x4 proj;
        float4x4 proj_inv;
        float4x4 viewProj;
        float4x4 viewProj_inv;
        float4 planes[6];
        float4 worldPos;
        
        //previous
        float4x4 previousViewProj;
        float4x4 previousViewProj_inv;
        float4 previousWorldPos;
        
        float sizeCulling;
        float fovY;
        float nearClip;
        float farClip;
    };
    
    enum class LightType
    {
        Directional,
        Spot,
        Point
    };
    struct Light
    {
        float4 pos;
        float4 dir;
        float4 color;
        float range;
        float angle;
        float size;
        LightType type;
    };
    
    struct Froxels
    {
#ifdef __cplusplus
        uint resolution[3];
        uint index;
#else
        uint3 resolution;
        uint index;
#endif
    };
    
    struct AtmosphericScatteringParameters
    {
        uint froxelsIndex;
        uint currentFroxelIndex;
        uint historyFroxelIndex;
        float density;
        float luminosity;
        float specialNear;
    };
    
    struct PostProcessHalfResParameters
    {
        uint froxelsIndex;
        uint atmosphericScatteringIndex;
        uint lightedIndex;
        uint transparencyLayerIndex;
    };
    
    struct TAAParameters
    {
        uint lightedIndex;
        uint historyIndex;
    };
    
    struct PostProcessParameters
    {
        uint inputIsFullResolution;
        uint lightedIndex;
        uint postProcessedIndex;
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
    static const float brightnessClippingAdjust = 1;
    
    // ----------------- RT stuff ------------------
    static const uint maxRTDepth = 1;
    struct RTParameters
    {
        uint BVH;
        uint giIndex;
        uint directReservoirIndex;
        uint previousDirectReservoirIndex;
        uint giReservoirIndex;
        uint previousgiReservoirIndex;
        uint lightedIndex;
        uint specularHitDistanceIndex;
        uint maxFrameFilteringCount;
        float reservoirRandBias;
        float reservoirSpacialRandBias;
        float spacialRadius;
        uint passNumber;
        
        float SHARCSceneScale;
        uint SHARCEntriesNum;
        uint SHARCHashEntriesBufferIndex;
        uint SHARCAccumulationBufferIndex;
        uint SHARCResolvedBufferIndex;
        uint SHARCAccumulationFrameNum;
        uint SHARCStaleFrameNum;
        bool SHARCEnableAntifirefly;
        uint SHARCSamplesPerPixel;
        float SHARCRadianceScale;
        float SHARCRoughnessThreshold;
        
        uint bouncesMax;
        float throughputThreshold;
        float probeDownsampling;
        
        bool enableBackFaceCull;
        bool enableLighting;
        bool enableTransmission;
        bool enableRussianRoulette;
        bool enableSoftShadows;
    };
    
    struct GIReservoir
    {
        float3 dir;
        float dist;
        float3 color;
        float W;
        float Wsum;
        float Wcount;
    };
    
    struct GIReservoirCompressed
    {
        uint dir;
        uint color;
        uint Wcount_W;
        uint dist_Wsum;
    };
    // ----------------- End RT stuff ------------------
    
    //----------------------- DEBUG -----------------------
    struct EditorContext
    {
        uint rays : 1;
        uint boundingVolumes : 1;
        uint albedo : 1;
        uint normals : 1;
        uint clusters : 1;
        uint lighting : 1;
        uint GIprobes : 1;
        uint GIBounces : 1;
        uint debugBufferHeapIndex;
        uint debugVerticesHeapIndex;
        uint debugVerticesCountHeapIndex;
        uint selectionResultIndex;
    };
    
    struct SelectionResult
    {
        uint objectID;
    };
}
#endif // __STRUCTS__