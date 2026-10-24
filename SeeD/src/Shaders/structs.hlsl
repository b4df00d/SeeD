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

// A bool is 32 bits in an HLSL constant buffer but one byte in C++, so a shared
// struct using `bool` silently desyncs every member laid out after the first one
// -- the tail ends up reading past what was uploaded. Use hbool for anything
// that crosses the boundary.
#ifdef __cplusplus
#define hbool uint
static_assert(sizeof(hbool) == 4, "hbool must match HLSL's 32-bit bool");
#else
#define hbool bool
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
        uint fogVolumesHeapIndex; // StructuredBuffer<FogVolume>, read only while fogVolumeCount > 0
        uint fogVolumeCount;
    };
    
    enum class Upscaling
    {
        none,
        taa,
        dlss,
        dlssd
    };

    // Sky atmosphere (Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere Rendering
    // Technique"). Lengths in km, coefficients in 1/km; the planet surface is world y = 0.
    // float4 only: hlsl++ reserves 16 bytes per float3 on the CPU, scalars ride in .w.
    struct Atmosphere
    {
        float4 rayleighScattering; // xyz: scattering at ground, w: scale height
        float4 mieScattering;      // xyz: scattering at ground, w: scale height
        float4 mieAbsorption;      // xyz: absorption at ground, w: Henyey-Greenstein g
        float4 ozoneAbsorption;    // xyz: absorption at the tent peak (25 km, 30 km wide), w: unused
        float4 groundAlbedo;       // xyz, w: unused
        float4 sunDirection;       // xyz: unit vector toward the sun, w: angular radius in radians
        float4 sunIlluminance;     // xyz: W/m^2 at the top of the atmosphere (the sun light's color * intensity), w: 1 if the scene has a sun
        float bottomRadius;
        float topRadius;
        uint transmittanceLUT;     // SRV heap slots, invalidUINT until SkyAtmosphere::On ran
        uint multiScatteringLUT;
        uint skyViewLUT;
        uint aerialPerspectiveLUT; // camera froxels (screen uv, squared depth over 128 km): rgb in-scatter with the sun applied, a = 1 - mean transmittance
        uint pad1;
        uint pad2;
    };

    // Surface haze maps (hazeMap.hlsl): one per active haze layer, a top-down, camera-centered,
    // toroidal world-space map of the highest upward-facing surface of that layer's materials per
    // texel, splatted by the GBuffer pixel shaders and read by FogMedium. HazeMapLevels levels side by
    // side in one texture (level L at x offset L * resolution), each covering HazeMapCoverage[L] m.
    static const uint HazeMapLevels = 2;
    static const uint HazeLayerCount = 16;  // Components::HazeLayer::layer and the material's haze layer index, 0 = no haze
    static const float HazeHeightRange = 8192.0f; // world y in [-range/2, range/2) quantized on 24 bits
    struct HazeMap
    {
        uint layersIndex;     // SRV StructuredBuffer<HazeLayer>[HazeLayerCount], invalidUINT when the view has no haze maps
        uint activeMask;      // bit i: layer i has maps this frame
        float time;           // s, the haze noise animation
        uint pad0;
    };

    // Components::HazeLayer + its maps, uploaded every frame (MainView::UploadAndSetup), indexed by layer
    struct HazeLayer
    {
        float4 albedoDensity; // xyz: single-scattering albedo, w: extinction at the surface (1/m)
        float4 windG;         // xyz: noise scroll (m/s), w: Henyey-Greenstein g
        float height;         // m, density falls off as exp(-h / height) above the surface
        float slab;           // m below the top surface counted as inside the medium (vegetation canopy)
        float noiseFrequency; // 1/m
        float noiseThresholdLow;
        float noiseThresholdHigh;
        float upThreshold;    // minimum world normal y (|y| on double-sided materials) of a splatting surface
        uint blurRadius;      // texels: the resolve averages this frame's splats over this radius
        uint resolution;      // texels per level side

        int4 origin;          // this frame's window origin cell (x, z) per level: xy level 0, zw level 1
        int4 previousOrigin;  // last frame's, the windows the read map was resolved with
        float4 texelSize;     // xy: m per texel for level 0 / 1

        uint splatIndex;      // UAV RWTexture2D<uint>: this frame's InterlockedMax splats (quantized height), invalidUINT when inactive
        uint writeIndex;      // UAV RWTexture2D<uint2>: resolved this frame (x: quantized height, 0 = none, y: age in frames)
        uint readIndex;       // SRV Texture2D<uint2>: resolved last frame, the resolve history and what FogMedium samples
        uint readable;        // 0 on the frame the maps were (re)created: FogMedium (compute queue) may run before GBuffers clears them

        float coverage;       // 0..1, multiplies each texel's age coverage
        float pad1;
        float pad2;
        float pad3;
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
        uint instanceMeshletRangesIndex;
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
        uint normalUAVIndex; // same resource's UAV slot -- currently unused: the gbuffer pass writes the final full-range normal + roughness, nothing rewrites it
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
        uint meshletsCulledArgsSRVIndex; // read-only views of the culled lists, for the GBuffer mesh shaders
        uint meshletsCulledArgsSortedSRVIndex;
        uint meshDispatchArgsIndex; // one MeshDispatchArgs per shader bucket (UAV, written by BuildMeshDispatchArgs)
        Atmosphere atmosphere; // 16-byte aligned on both sides
        HazeMap hazeMap;       // after atmosphere: one 16-byte row
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
        // meshopt normal cone (backface cluster culling, culling.hlsl): xyz = axis, w = cutoff.
        // float4 rather than a packed uint so the C++ (hlslpp, 16-byte aligned float4) and HLSL
        // strides both stay 48 bytes.
        float4 cone;
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

        // LOD the high-detail ray-tracing BLAS is built from (MeshStorage::LoadBLAS): 1 for opaque
        // multi-LOD meshes (fewer triangles to traverse; primary visibility is rasterised from LOD0),
        // 0 when an OMM is linked (the micromap is indexed by LOD0 triangles). GetRTSurfaceData
        // fetches hit triangles from this LOD. The pads keep the struct a 16-byte multiple.
        uint rtHighLod;
        uint rtPad0;
        uint rtPad1;
        uint rtPad2;

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
    static const uint MaterialDoubleSidedParam = 10; // parameters[10] != 0 : double-sided (CULL_NONE, no meshlet cone culling)
    static const uint MaterialHazeLayerParam = 11;   // parameters[11] : haze layer splatted into the haze map (HazeLayer index, 0 = none)
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
    
    // One per instance, indexed by instance. CullingInstances writes the selected LOD's meshlet
    // count (0 if culled) into meshletPrefix, InstanceMeshletPrefix scans it in place into an
    // exclusive prefix, and each CullingMeshlets thread binary-searches it for its owning instance.
    struct InstanceMeshletRange
    {
        uint meshletPrefix;
        uint firstMeshlet;
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
    
    // One culled meshlet. Not an indirect command: each GBuffer mesh-shader group fetches its own
    // entry by flattened SV_GroupID (meshCommon.hlsli LoadMeshletShared), so a whole shader bucket
    // draws with a single DispatchMesh instead of one ExecuteIndirect command per meshlet.
    struct MeshletDraw
    {
        uint instanceIndex;
        uint meshletIndex;
    };

    // One indirect command per shader bucket, built by culling.hlsl BuildMeshDispatchArgs.
    // meshletCount goes to the b6 root constant (the mesh shader's out-of-range guard).
    struct MeshDispatchArgs
    {
        uint meshletCount;

        // for D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH
        uint ThreadGroupCountX;
        uint ThreadGroupCountY;
        uint ThreadGroupCountZ;
    };
    // DispatchMesh caps every dimension at 65535 groups and X*Y*Z at 2^22, so a bucket is laid out
    // as a 2D grid of fixed width; groups past meshletCount in the last row early-out.
    static const uint meshDispatchWidth = 1024;
    static const uint meshDispatchMaxGroups = 1u << 22;
    // set in the b5 root constant (draw base offset) when the bucket reads meshletsCulledArgsSorted
    static const uint meshDrawSortedBit = 0x80000000u;
    
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
    
    // Components::FogVolume, uploaded every frame (MainView::UpdateFogVolumes)
    struct FogVolume
    {
        float4 sphere;       // xyz: world center, w: radius (m)
        float4 colorDensity; // xyz: single-scattering albedo, w: extinction at the center (1/m)
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
    
    // Local fog in the froxels (the planet atmosphere is the aerial perspective volume, see Atmosphere)
    struct AtmosphericScatteringParameters
    {
        uint froxelsIndex;
        uint currentFroxelIndex;
        uint historyFroxelIndex;
        float specialNear;         // view distance (m) of froxel slice 1, slices are exponential from there to the far clip
        float4 fogScattering;      // xyz: 1/m at density 1, w: Henyey-Greenstein g
        float4 fogAbsorption;      // xyz: 1/m at density 1, w: fog height (world y, m) below which the height term is 1
        float heightFalloff;       // 1/m, density = exp(-(y - fogHeight) * heightFalloff) above fogHeight
        float noiseFrequency;
        float noiseThresholdLow;
        float noiseThresholdHigh;
        float animationSpeed;
        float fogDensity;          // global density multiplier
        float pad1;
        float pad2;
    };
    
    struct SkyAtmosphereParameters
    {
        uint transmittanceLUTUAV;
        uint multiScatteringLUTUAV;
        uint skyViewLUTUAV;
        uint aerialPerspectiveLUTUAV;
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
        uint tonemapper;
    };
    static const float brightnessClippingAdjust = 1;
    // PostProcessParameters::tonemapper (also RenderSettingsVolume::tonemapper and its combo order)
    static const uint TonemapperNone       = 0;
    static const uint TonemapperGT         = 1;
    static const uint TonemapperACES       = 2;
    static const uint TonemapperAgX        = 3;
    static const uint TonemapperAgXPunchy  = 4;
    static const uint TonemapperPBRNeutral = 5;
    
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
        hbool SHARCEnableAntifirefly;
        uint SHARCSamplesPerPixel;
        float SHARCRadianceScale;
        float SHARCRoughnessThreshold;
        
        uint bouncesMax;
        float throughputThreshold;
        float probeDownsampling;
        
        hbool enableBackFaceCull;
        hbool enableLighting;
        hbool enableTransmission;
        hbool enableRussianRoulette;
        hbool enableSoftShadows;
        hbool directOnly;   // drop the ReSTIR indirect term; matches Cycles at max_bounces 0
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
    static const uint EditorDebugHazeMap         = 1u << 12;

    struct EditorContext
    {
        uint debugFlags;
        uint debugBufferHeapIndex;
        uint debugVerticesHeapIndex;
        uint debugVerticesCountHeapIndex;
        uint cpuDebugVerticesHeapIndex; // CPU-authored lines (physics colliders), drawn directly
        uint selectionResultIndex;
        uint debugHazeLayer; // layer shown by EditorDebugHazeMap
    };
    
    struct SelectionResult
    {
        uint objectID;
    };
}
#endif // __STRUCTS__