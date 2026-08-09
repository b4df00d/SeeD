// ============================================================================
// TODO list  
// ============================================================================
//
// [X] 1. Shader Execution Reordering (SER)
//        https://devblogs.microsoft.com/directx/shader-execution-reordering/
//        - Target the EXISTING DXR pipeline. raytracing2.hlsl already has
//          raygeneration/miss/closesthit/anyhit shaders + TraceRay/DispatchRays,
//          so no DXR rewrite is needed.
//        - Use MaybeReorderThread to reorder by hit/material coherence AFTER
//          TraceRay returns, to cut shading divergence (threads in a warp that
//          hit different materials/closest-hit paths get regrouped).
//        - Biggest win is exactly this multi-material pipeline case.
//
// [X] 2. Opacity Micro Map (OMM)
//        https://github.com/NVIDIA-RTX/OMM
//        - Cutout already works in RT but is costly: it goes through the any-hit
//          shaders in raytracing2.hlsl (alpha test in any-hit).
//        - OMM lets the RT cores skip most any-hit invocations (known-opaque /
//          known-transparent micro-triangles), keeping any-hit only for the
//          unknown border region.
//        - Touches the same RT shaders as SER -> implement alongside point 1.
//
// [X] 3. Simple terrain with Erosion
//        https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html
//        - GPU "terrain paint" pass: compute the base heightmap + erosion on the
//          GPU and store the result into a final heightmap texture.
//        - Geometry: ONE clustered terrain grid mesh stored ONCE in mesh storage,
//          instanced across a quadtree (the quadtree drives instance/LOD
//          selection and per-node world extents). Think a clean mesh separation
//          from the pixel shader and height deformation so we can swap the quadtree
//          for a Concurent Binary Tree https://github.com/AnisB/large_cbt
//        - Deformation: displace vertices in the MESH SHADER by sampling the
//          final heightmap at render time (no baked geometry, no duplicated
//          storage per node).
//        - The terrain will be an ECS component. The base heightmap (before erosion) 
//          will be computed from an initial map plus some additionnal brushes, also
//          ECS components with a local heightmap texture that will be blended into 
//          the base heightmap via a compute shader.
//        - The brushes will be painted in the editor, and the terrain will be updated
//          in real time when a toggle is set to true on the terrain component.
//        - Once the terrain is finalized, the toggle will be set to false and the 
//          terrain will be baked into a final heightmap texture.
//
// [X] 4. Reduce mesh storage and vertex strides  (FULL commit, no fallback)
//        - Meshlet indices on 8 bits.
//        - Vertex positions on 16 bits, LOCAL to the cluster center: store a
//          per-cluster center + extent/scale and reconstruct world/local pos in
//          the mesh shader (adjust the vertex fetch accordingly).
//        - This is foundational: terrain (3) and skinning (6) build on it.
//        - WATCH: GPU culling has a latent empty-LOD bug (far instances vanish if
//          the selected LOD has 0 meshlets) -- re-check culling after changing
//          the meshlet/cluster format.
//
// [X] 5. Project file
//        - Simple .txt format (like the existing .seed / UILayout.txt files).
//        - Stores: asset directory, scene to load, and window/render settings.
//
// [ ] 6. Mesh skinning feature
//        - PREREQUISITE: Assimp bone-list loading must be done first (see point 7
//          dependency: bones come from asset loading).
//        - Approach: duplicate the mesh per skinned instance; a compute shader
//          reads the ORIGINAL mesh and writes the transformed (skinned) copy.
//
// [ ] 7. Simple animation system
//        - Bone list comes from Assimp asset loading.
//        - Feeds the bone matrices consumed by the skinning compute shader (6).
//
// [ ] 8. Jolt integration with ECS
//        - ECS components: rigidbody, collisionmesh, constraint.
//        - System A: initializes Jolt bodies/constraints from the ECS components.
//        - System B: reads transforms back from Jolt and writes them into the ECS
//          each frame.
//
// [X] 9. Execute pass command buffers as soon as they are ready, in the correct
//        order -- WITHOUT locking a dedicated thread just to do the submission.
//
// [O] 10. DLSS parameters UI: model, quality.
//         Changing quality is craching
//
// [X] 11. GBuffer draw calls (ExecuteIndirect) must be able to run different
//         shaders -- at least one with early-Z and one without -- so cutout works
//         in the GBuffer (mirrors the RT cutout handling).
// 
// [X] 12. Do a better component metadata parsing
//         for the moment it´s only getting components in world.h, but it should
//         be able to parse any header file and get all the components in it.
//         - GenerateComponentMetaData() (ComponentMetaTypes.h) walks EVERY .h of SeeD\src, so a
//           component can live next to the system that owns it. ComponentMetaData.h is now
//           included from Main.cpp (after the last header) instead of from World.h.
//         - the field scan follows brace depth, so a nested enum/struct no longer truncates the
//           member list (State had NO members, and trailing fields were being dropped silently)
//         - members carry their exact sizeof, components their alignof: VerifyKnownComponents can
//           now prove the table covers the whole struct, and Load stops guessing sizes from the
//           next member's offset
// 
// [ ] 13. Particule system that will be able to feed some atmospheric scattering
//         data in the froxels (via simple spheres at first)
// 
// [ ] 14. texture importer does not work correctly when texture is not 32bits and pow2
// 
// [ ] 15. deal with this comment in renderer.h :
//         // Path registration only (no entity, no bucket) for the other built-in mesh-shader variants.....
// 
// [ ] 16. All task scheduling allocates a lot. 
//         See to recreate the scheduling only when not the same as the previous frame.
//
// [X] 17. rework MeshData to store compressed vertices to avoid Pack vertices each loading.
//         - PackMeshVertices (GPU.h) runs once at import, from MeshLoader::Process; MeshData and the
//           .mesh file carry VertexPacked + the AABB, so GetOrCreate is a straight upload
//         - .mesh gained a magic + version : an entry from an older build is refused with a message
//           instead of being read as garbage (delete the project cache folder and re-import)
// 
// [ ] 18. add default values ins component metadata, and use them when the component is created
//         and to reset the component to default values (in global or one component at a time)
// 
// [X] 19. rework omm resources (in a new struct with 2 entries for high and low ?)
//         maybe even drop the low res omm. Put all in 1 function (formating / cleaning code)
//         - dropped the low-res omm: only the LOD0 BLAS carries one now
//         - GPU resources moved into Mesh::OMM (array / indices / inputs / indexFormat / textureHash)
//         - OMMData flattened (no more LOD sections), sidecar bumped to version 2
//         - one bake function (OMMBake::BakeAndWrite) and one build function (BuildOMMArray)
// 
// [ ] 20. avoid multiple upload() per frame for StructuredUploadBuffer. They can flag as dirty once
//         and only upload once per frame. (see StructuredUploadBuffer::Upload() )
// 
// [ ] 21. add gpu profile for tracy : tracy gpu zone with multithreaded command buffer recording is not working well.
//         it create a lane per thread that did the recording... not nice
// 
// [ ] 22. add a validation and conversion function to convert old version of the world to the new one 
//         (for example if a component is removed or added) and add a UI function to start the conversion
// 
// [ ] 23. better resource transition. always return to the comon state after a pass ?
//         since this is the end of a commandlist for each pass, the transition event should not degrade the performance.
// 
// [ ] 24. DLSS hit distance for reflections are not working correctly. reflection are blurry when moving.
//
// [X] 25. shader picker for Components::Shader: let the user pick a .hlsl file, scan it for the
//         #pragma <stage> <PublicName> <entry...> lines it declares (the same convention the shader
//         compiler itself parses in Loading.h), and pick one of the found variations to fill the
//         Shader entity's path field, instead of typing the "path|entry" string by hand.
//         - ShaderLoader::ParsePragma/ListVariations (Loading.h) read the pragmas; Parse() now
//           uses the same reader, so the picker can only offer what the compiler would find
//         - Components::ShaderPropertyDraw (UI.h) is the file + variation combo pair
// 
// [X] 26. add feature validation for DMA/GPUUpload: check if the GPU supports it, and if not, fall back to CPU upload.
//         - GPU::features.gpuUploadHeap comes from OPTIONS16.GPUUploadHeapSupported (needs ReBAR);
//           Resource::CreateBuffer(upload) picks GPU_UPLOAD or plain UPLOAD from it, with the
//           initial state each heap requires (COMMON vs GENERIC_READ).
// 
// [ ] 27. do a smater system to check cpu and gpu struct sizes and alignments. (structs.h)
//
// [ ] 28. PSO render target formats are guessed from PS reflection and do not match the textures
//         they are bound to. ShaderLoader fills shader.outputs from the PS output signature
//         (Loading.h ~2860), so a "float4 : SV_Target0" always becomes R32G32B32A32_FLOAT. The
//         shaders already declare the real format in the comment after the semantic, but the
//         parser meant to read it is commented out (Loading.h ~3094), so the comment is currently
//         decoration. Every graphics PSO branch (gBuffer/forward/debug) then copies the guessed
//         formats into RTFormats. Current mismatches:
//         - mesh.hlsl: 5 of 6 slots wrong (albedo/specularAlbedo R32G32B32A32_FLOAT vs
//           R8G8B8A8_UNORM, normal vs R16G16B16A16_FLOAT, metalness R32_FLOAT vs R8_UNORM,
//           motion R32G32_FLOAT vs R16G16_FLOAT). Only instanceID (uint -> R32_UINT) matches.
//         - terrainmesh.hlsl: same shape.
//         - debug.hlsl: R32G32B32A32_FLOAT vs 'lighted' R16G16B16A16_FLOAT.
//         D3D12 only validates this on an EXECUTED draw (ERROR #613
//         RENDER_TARGET_FORMAT_MISMATCH_PIPELINE_STATE), not at PSO creation or
//         OMSetRenderTargets -- and the gBuffer draws via ExecuteIndirect, which does nothing when
//         the GPU-side count is 0. That is why this has stayed hidden: it only fires once
//         something actually draws, and only under the debug layer. In Release it is undefined
//         behaviour that drivers tend to absorb silently.
//         Fix: finish the SV_Target comment parser and feed it into shader.outputs so the
//         declared format wins over the reflected one; the comments already name the right
//         formats everywhere except debug.hlsl (needs R16G16B16A16_FLOAT for 'lighted').
//         Then assert at pass setup that each bound RT's format equals the PSO's slot format,
//         so the next mismatch is caught at bind time instead of on the first draw.
//         Related: resources bound as RTs must be created with CreateRenderTarget, not
//         CreateTexture -- 'lighted' was CreateTexture (SRV+UAV only), so its RTV handle was null
//         and OMSetRenderTargets died with INVALID_DESCRIPTOR_HANDLE (#646). Fixed for 'lighted';
//         the same trap applies to anything else that later becomes a render target.
//
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cassert>

#ifdef _X86_
#define BreakPoint()        _asm { int 3h }
#else
#define BreakPoint()        DebugBreak()
#endif


#ifdef _DEBUG
#define seedAssert(condition) if(!(condition)) BreakPoint();
#else
#define seedAssert(condition) (void)0
#endif

#include "../../Third/hlslpp-master/include/hlsl++.h"
using namespace hlslpp;
#include "HLSL_Extension.h"

#define TRACY_ENABLE
#define TRACY_ON_DEMAND
#define TRACY_NO_SYSTEM_TRACING
#include "../../Third/tracy-master/public/tracy/Tracy.hpp"
#include "../../Third/tracy-master/public/TracyClient.cpp"
#ifdef TRACY_ENABLE
void* operator new(std::size_t count)
{
    auto ptr = malloc(count);
    TracyAlloc(ptr, count);
    return ptr;
}
void operator delete(void* ptr) noexcept
{
    TracyFree(ptr);
    free(ptr);
}
#endif

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "Containers.h"
#include "String.h"


struct LightUnitEntry
{
    const char* name;
    float multiplier;
};
static constexpr LightUnitEntry lightUnitTable[] = {
    { "Candela (cd)",         1.0f        },
    { "Kilolux (klx)",        1000.0f     },
    { "Megalux (Mlx)",        1000000.0f  },
    { "Watts (680 lm/W)",     680.0f      },
    { "EV 0  (x1)",           1.0f        },
    { "EV 6  (x64)",          64.0f       },
    { "EV 10 (x1024)",        1024.0f     },
    { "EV 14 (x16384)",       16384.0f    },
    { "Custom",               0.0f        },
};

struct Options
{
    bool stopFrustumUpdate = false;
    bool stopBufferUpload = false;
    bool enableStructuredCommandBuffersReadback = false;
    bool stepMotion = false;
#ifdef _DEBUG
    bool shaderReload = true;
#else
    bool shaderReload = false;
#endif
    bool shaderStatsCsv = true;
    bool drawPhysicsShapes = false;
    bool frontToBackSort = true;
    float sortMaxDistance = 512.0f;
    float lodDistanceMultiplier = 0.25f;
    float distanceCullingValue = 6.0f;
    float distanceCullingValueRT = 6.0f;
    int lightUnitsIndex = 0;
    float customLightMultiplier = 1.0f;

    enum class DebugMode
    {
        none,
        ray,
        boundingSphere,
    };
    DebugMode debugMode = DebugMode::none;

    enum class DebugDraw
    {
        none,
        albedo,
        normals,
        clusters,
        triangles,
        lighting,
        GIprobes,
        GIBounces,
        GIAlbedo,
        GINormals,
        overdraw,
    };
    DebugDraw debugDraw = DebugDraw::none;
} options;


#include "Time.h"
#include "IO.h"
#include "Tasks.h"
#include "GPU.h"

// Defined in Renderer.h: the main view's camera for this frame, false while the renderer is not up
// yet. Declared here so Terrain.h can be included before Renderer.h -- the terrain components have
// to be declared before the renderer queries them, and this is all its streaming system needs back.
bool GetMainViewCamera(HLSL::Camera& camera);

#include "World.h"
#include "UI.h"
#include "Loading.h"
#include "Terrain.h"   // terrain components + systems: after Loading.h (MeshLoader/AssetLibrary),
#include "Physic.h"    // Jolt + the physics components; needs ComponentBase/Handle from World.h only
#include "Renderer.h"  // before Renderer.h, which queries Components::Terrain
#include "Engine.h"
#include "Project.h"
#include "Editor.h"

// Autogenerated component reflection table (GenerateComponentMetaData, ComponentMetaTypes.h).
// Included LAST: its body does offsetof/sizeof/alignof on every component, so every header that
// declares one must have been seen. ComponentMetaTypes.h forward-declares InitKnownComponents()
// for the callers above.
#include "ComponentMetaData.h"

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    Engine engine;

#ifdef _DEBUG
    engine.ios.window.usevSync = true;
#endif
    engine.ios.window.windowInstance = hInstance;

    Project project;
    project.Load(lpCmdLine ? String(lpCmdLine) : String(""));
    project.Setup(engine);

    engine.On();

    if (!project.startupScene.empty())
    {
        editorState.currentWorldPath = project.SceneAbs(project.startupScene);
        World::instance->Load(editorState.currentWorldPath);
    }

    engine.Loop();
    engine.Off();

    return 0;
}