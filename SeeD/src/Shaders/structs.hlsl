#pragma once
#ifndef __STRUCTS__
#define __STRUCTS__

#define GROUPED_CULLING_THREADS 128
#define INSTANCE_CULLING_THREADS 128
#define DRAW_SORT_THREADS 128
#define SORT_BUCKETS 256 // depth bins for front-to-back draw ordering

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
    
    // Data structure to match the command signature used for ExecuteIndirect.
    #ifdef __cplusplus // bah c�est surtout pour par avoir ca dans le HLSL
    #else
    typedef uint64_t D3D12_GPU_VIRTUAL_ADDRESS;
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
    
    struct D3D12_RAYTRACING_INSTANCE_DESC
    {
        float Transform[ 3 ][ 4 ];
        uint InstanceID	: 24;
        uint InstanceMask : 8;
        uint InstanceContributionToHitGroupIndex : 24;
        uint Flags : 8;
        D3D12_GPU_VIRTUAL_ADDRESS AccelerationStructure;
    };
    
    enum D3D12_RAYTRACING_INSTANCE_FLAGS
    {
        D3D12_RAYTRACING_INSTANCE_FLAG_NONE	= 0,
        D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE	= 0x1,
        D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE	= 0x2,
        D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE	= 0x4,
        D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE	= 0x8,
        D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OMM_2_STATE	= 0x10,
        D3D12_RAYTRACING_INSTANCE_FLAG_DISABLE_OMMS	= 0x20
    };
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
        uint meshletsToCullIndex;
        uint meshletsCulledArgsIndex; // single shared draw-args buffer; each shader bucket owns a region at its prefix-sum base offset (see shaderBucketOffsetsIndex)
        uint meshletsCulledArgsSortedIndex;
        uint sortHistogramIndex;
        uint meshletBucketsIndex;
        uint frontToBackSort;
        uint instancesCounterIndex;
        uint meshletsCounterIndex; // one counter per shader bucket now (was fixed 3: opaque/cutout/terrain)
        uint shaderBucketOffsetsIndex; // heap index of a StructuredBuffer<uint>: exclusive-prefix-sum base offset per shader bucket, into meshletsCulledArgs
        uint shaderBucketCount; // number of shader buckets this frame (CullingReset zeroes exactly this many meshletsCounters entries)
        uint albedoIndex;
        uint metalnessIndex;
        uint normalIndex; // gbuffer normal, a = roughness (DLSS-RR packed mode); SRV slot, every reader (GetGBufferCameraData) samples it read-only
        uint normalUAVIndex; // same resource's UAV slot -- lighting.hlsl is the one writer (packs roughness into .a via RWTexture2D), needs a heap index of the matching descriptor type
        uint motionIndex;
        uint instanceIDIndex;
        uint overdrawIndex;
        uint depthIndex;
        uint reverseZ;
        uint HZB;
        uint HZBMipCount;
        float textureLODBias;
        float sortMaxDistance; // world-space distance mapped to the farthest front-to-back sort bucket
        float lodDistanceMultiplier; // scales distance in mesh LOD selection (culling.hlsl); higher = drop to coarser LODs sooner
        float distanceCullingValue; // instances whose unclamped LOD exceeds this are culled entirely (culling.hlsl); higher = keep farther instances
        float distanceCullingValueRT; // same threshold for the raytracing instances; the TLAS entry stays (fixed count) but points at a null BLAS
    };
    
    struct Shader
    {
        uint id;
    };
    
    // must match GPU.h VertexPacked (plain layout, 24 bytes).
    // packedPos: SNORM16 x,y,z LOCAL to the mesh AABB, at offset 0 so the BLAS can read it
    //   directly as R16G16B16A16_SNORM (decode with Mesh.aabbMin / Mesh.aabbExtent, see mesh.hlsl).
    //   The 4th SNORM16 (high 16 bits of packedPos.y) stores the TBN handedness sign (+/-1),
    //   ignored by the BLAS; used to rebuild the binormal = cross(normal,tangent)*handedness.
    // normalOct/tangentOct: unit vectors octahedral-packed via octahedral_32(v,16) / i_octahedral_32.
    struct Vertex
    {
        uint2 packedPos;
        float2 uv;
        uint normalOct;
        uint tangentOct;
    };

    // Debug-only vertex (kept full precision, decoupled from the mesh Vertex format).
    // must match GPU.h DebugVertex
    struct DebugVertex
    {
        float3 pos;
        float3 color;
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
        // .w > 0.5 on EITHER field marks this as a terrain override mesh whose vertex range holds
        // already-baked (displaced) geometry (MeshStorage::CreateMeshOverride /
        // terrainMeshBake.hlsl) -- terrainmesh.hlsl's MeshMainTerrain checks aabbMin.w and skips
        // its live heightmap-sampling displacement for these, or the baked + live displacement
        // would stack. Regular (non-terrain) meshes always have aabbMin.w == 0.
        float4 aabbMin;    // .xyz : mesh-AABB min  (anchor for SNORM16 position decode)
        float4 aabbExtent; // .xyz : mesh-AABB size (max - min)
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

        // Bounding sphere derived from the AABB (object space): xyz = center, w = radius.
        // Matches the previously stored value (center = (min+max)/2, radius = length(max-min)/2).
        float4 GetBoundingSphere()
        {
            float3 c = aabbMin.xyz + aabbExtent.xyz * 0.5;
            return float4(c, length(aabbExtent.xyz) * 0.5);
        }
    };
    
    static const uint MaterialTextureCount = 7;
    static const uint MaterialParametersCount = 24;
    struct Material
    {
        float parameters[MaterialParametersCount];
        uint textures[MaterialTextureCount];
        uint shaderIndex; // stable per-shader-bucket index (Renderer.h MainView shader-bucket registry); routes this material's meshlets to the matching draw bucket in culling.hlsl instead of the old cutout/terrain bool flags
    };
    
    struct TerrainErosionParameters
    {
        uint inputHeightmapIndex;    // bindless SRV of the raw imported heightmap
        uint outputHeightmapIndex;   // bindless UAV of the eroded R16_UNORM map
        uint outputResolution;       // TerrainErosion::ErodedResolution
        uint octaves;                // EROSION_OCTAVES

        float scale;                 // EROSION_SCALE: horizontal+vertical scale, fraction of the terrain footprint
        float strength;              // EROSION_STRENGTH
        float gullyWeight;           // EROSION_GULLY_WEIGHT
        float detail;                // EROSION_DETAIL

        float lacunarity;            // EROSION_LACUNARITY: per-octave frequency multiplier
        float gain;                  // EROSION_GAIN: per-octave strength multiplier
        float cellScale;             // EROSION_CELL_SCALE: phacelle cell size relative to erosion scale
        float normalization;         // EROSION_NORMALIZATION: phacelle magnitude normalization degree

        float ridgeRounding;         // EROSION_ROUNDING.x
        float creaseRounding;        // EROSION_ROUNDING.y
        uint  outputDiffIndex;       // bindless UAV of the R8 difference map (see TerrainErosionDiffParam)
        // Shared with the Noise entry (TerrainNoiseMain, same file/CBV): decorrelates the procedural
        // heightmap pattern (same params + seed -> identical terrain). Unused (0) by Erosion.
        uint  seed;
    };

    // Terrain node vertex-bake (MeshStorage::CreateMeshOverride / terrainMeshBake.hlsl): displaces
    // one node's copy of the shared grid mesh once on the GPU, so raster (terrainmesh.hlsl) and RT
    // (via the override's own BLAS) both see the same baked geometry instead of the gbuffer path
    // re-sampling the heightmap every frame. One dispatch per dirty node, one thread per vertex.
    struct TerrainBakeParameters
    {
        uint verticesUAVIndex;      // bindless UAV of MeshStorage::vertices (same resource read + written)
        uint heightmapIndex;        // eroded (if available) or raw heightmap SRV to sample
        uint sourceVertexOffset;    // source mesh's base into the shared vertices pool
        uint outputVertexOffset;    // override mesh's own base into the shared vertices pool

        uint vertexCount;           // shared by source and override (identical grid topology)
        float worldExtent;          // terrain's XZ footprint (UV = worldXZ / worldExtent + 0.5)
        float heightScale;
        float heightOffset;

        float4 sourceAabbMin;       // decode domain for the source's SNORM16 positions (.w unused)
        float4 sourceAabbExtent;
        float4 outputAabbMin;       // encode domain for the override's SNORM16 positions (.w unused)
        float4 outputAabbExtent;

        // .xyz = node instance's Transform.position (terrain nodes: no rotation), .w = Transform.scale.x
        // (== .z, uniform XZ, Y always 1). Packed into one float4 (not a trailing float3+float) because
        // hlslpp's C++ float3 is backed by a full 16-byte __m128 (its .w lane is padding/garbage), while
        // HLSL's cbuffer packs a float3 as 12 bytes and slots a following scalar into the same 16-byte
        // row -- a trailing "float3;float;" pair reads back at the WRONG offset on the HLSL side (this
        // bit us once already: nodeScaleXZ silently read ~0, collapsing every node's XZ displacement).
        float4 nodeWorldPosScaleXZ;

        uint heightmapBlurRadius;  // box-blur radius in heightmap texels (0 = off, a single point sample)
    };


    // Packed into D3D12_RAYTRACING_INSTANCE_DESC.InstanceID (24 bits) by culling.hlsl when the
    // instance points at the low-detail BLAS, so hit shaders fetch triangles from the matching LOD.
    static const uint RTInstanceLowLodBit = 1u << 23;

    // Instance::rtFlags bits (written by UpdateInstances, consumed by culling.hlsl)
    // set when the instance's material differs from the one the mesh's OMM was baked against:
    // culling.hlsl then adds D3D12_RAYTRACING_INSTANCE_FLAG_DISABLE_OMMS for that instance
    static const uint RTInstanceFlagDisableOMMs = 1u << 0;

    struct Instance
    {
        float4x4 current; // FFS hlsl++ does store 4x3 and 4x3 in the same way ... BS ! TODO : make the 4x3 packing work
        float4x4 previous;
        uint meshIndex;
        uint materialIndex;
        uint objectID; // map to entityBase
        uint rtFlags;
        D3D12_GPU_VIRTUAL_ADDRESS rayTracingBLAS;
        D3D12_GPU_VIRTUAL_ADDRESS rayTracingBLASLow;
        // World-space bounding-sphere override for CPU-computed culling volumes the generic
        // mesh-AABB-derived sphere can't express (currently: terrain quadtree nodes, see World.h
        // Systems::TerrainStreaming::CreateNodeInstance). w <= 0 means "no override, use
        // mesh.GetBoundingSphere() transformed by worldMatrix" (culling.hlsl CullingInstances /
        // CullingMeshlets).
        float4 boundingSphereOverride;

        float3 GetPosition()
        {
            return float3(current[0][3], current[1][3], current[2][3]);
        }
        
        float GetScale()
        {
            return abs(max(max(length(current[0].xyz), length(current[1].xyz)), length(current[2].xyz)));
        }
        
        #ifdef __cplusplus // bah c�est surtout pour par avoir ca dans le HLSL
        float4x4 unpack(float4x4& mat)
        #else
        float4x4 unpack(float4x4 mat)
        #endif
        {
            return mat;
        }
        #ifdef __cplusplus // bah c�est surtout pour par avoir ca dans le HLSL
        float4x4 pack(float4x4& mat)
        #else
        float4x4 pack(float4x4 mat)
        #endif
        {
            return mat;
        }
    };
    
    struct StructuredCommandBufferParameters
    {
        uint commandIndex;
        uint commandCount;
        uint commandStride;
        uint bufferIndex;
        uint bufferCounterIndex;
        uint bufferStride;
    };
    

    struct IndirectCommand
    {
	    uint index;
        //uint stuff;
	    //D3D12_GPU_VIRTUAL_ADDRESS cbv;
	    D3D12_DRAW_ARGUMENTS drawArguments;
    };
    
    struct GroupedCullingDispatch
    {
        uint instanceIndex;
        uint meshletIndex;
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
        uint castShadow;
        LightType type;
        uint pad1;
        uint pad2;
        uint pad3;
    };
    
    struct Froxels
    {
#ifdef __cplusplus
        uint resolution[3];
        uint index;    // UAV heap slot: every AtmosphericScattering.hlsl compute pass writes this froxel volume via RWTexture3D
        uint srvIndex; // SRV heap slot: read-only Texture3D<>.Sample() access (Reprojection's history read, postprocessHalfRes's final composite read)
#else
        uint3 resolution;
        uint index;
        uint srvIndex;
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
        float heightFalloff;
        float noiseFrequency;
        float noiseThresholdLow;
        float noiseThresholdHigh;
        float animationSpeed;
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
        uint instancesRaytracingHeapIndex;
        uint instancesRaytracingCountHeapIndex;
        
        uint giReservoirIndex;
        uint previousgiReservoirIndex;
        uint lightedIndex;
        uint specularHitDistanceIndex;
        uint maxFrameFilteringCount;
        float reservoirRandBias;
        float reservoirSpacialRandBias;
        float spacialRadius;
        uint spacialSampleCount;

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
        float3 hitNormal; // sample-point geometric normal (needed for the spatial reuse Jacobian)
    };

    struct GIReservoirCompressed
    {
        uint dir;
        uint color;
        uint Wcount_W;
        uint dist_Wsum;
        uint hitNormal; // octahedral-packed
    };
    // ----------------- End RT stuff ------------------
    
    //----------------------- DEBUG -----------------------
    // EditorContext is raw-memcpy'd from C++ straight into a GPU cbuffer
    // (ConstantBuffer::PushConstantBuffer) and reinterpreted by DXC on the shader side -- nothing
    // guarantees the two compilers pack a run of C-style `uint x : 1` bitfields into a cbuffer
    // register the same way (unlike plain scalar fields, this was never actually verified for this
    // struct). A single explicit uint + named bit masks sidesteps the packing question entirely.
    static const uint EditorDebugRays            = 1u << 0;
    static const uint EditorDebugBoundingVolumes = 1u << 1;
    static const uint EditorDebugAlbedo          = 1u << 2;
    static const uint EditorDebugNormals         = 1u << 3;
    static const uint EditorDebugClusters        = 1u << 4;
    static const uint EditorDebugTriangles       = 1u << 5;
    static const uint EditorDebugLighting        = 1u << 6;
    static const uint EditorDebugGIprobes        = 1u << 7;
    static const uint EditorDebugGIBounces       = 1u << 8;
    static const uint EditorDebugGIAlbedo        = 1u << 9;
    static const uint EditorDebugGINormals       = 1u << 10;
    static const uint EditorDebugOverdraw        = 1u << 11;

    struct EditorContext
    {
        uint debugFlags;
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