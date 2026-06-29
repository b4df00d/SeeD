// ============================================================================
// TODO list  (self-contained: each point holds all the data needed to start a
//             dedicated coding session)
// ============================================================================
//
// [ ] 1. Shader Execution Reordering (SER)
//        https://devblogs.microsoft.com/directx/shader-execution-reordering/
//        - Target the EXISTING DXR pipeline. raytracing2.hlsl already has
//          raygeneration/miss/closesthit/anyhit shaders + TraceRay/DispatchRays,
//          so no DXR rewrite is needed.
//        - Use MaybeReorderThread to reorder by hit/material coherence AFTER
//          TraceRay returns, to cut shading divergence (threads in a warp that
//          hit different materials/closest-hit paths get regrouped).
//        - Biggest win is exactly this multi-material pipeline case.
//
// [ ] 2. Opacity Micro Map (OMM)
//        https://github.com/NVIDIA-RTX/OMM
//        - Cutout already works in RT but is costly: it goes through the any-hit
//          shaders in raytracing2.hlsl (alpha test in any-hit).
//        - OMM lets the RT cores skip most any-hit invocations (known-opaque /
//          known-transparent micro-triangles), keeping any-hit only for the
//          unknown border region.
//        - Touches the same RT shaders as SER -> implement alongside point 1.
//
// [ ] 3. Simple terrain with Erosion
//        https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html
//        - GPU "terrain paint" pass: compute the base heightmap + erosion on the
//          GPU and store the result into a final heightmap texture.
//        - Geometry: ONE clustered terrain grid mesh stored ONCE in mesh storage,
//          instanced across a quadtree (the quadtree drives instance/LOD
//          selection and per-node world extents).
//        - Deformation: displace vertices in the MESH SHADER by sampling the
//          final heightmap at render time (no baked geometry, no duplicated
//          storage per node).
//        - The base grid can reuse the lighter mesh format from point 4.
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
// [ ] 5. Project file
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
// [ ] 9. Execute pass command buffers as soon as they are ready, in the correct
//        order -- WITHOUT locking a dedicated thread just to do the submission.
//
// [O] 10. DLSS parameters UI: model, quality.
//         Changing quality is craching
//
// [ ] 11. GBuffer draw calls (ExecuteIndirect) must be able to run different
//         shaders -- at least one with early-Z and one without -- so cutout works
//         in the GBuffer (mirrors the RT cutout handling).
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
    bool shaderReload = true;
    bool shaderStatsCsv = true; // dump ..\shaderStats.csv for the live edit/profile loop
    bool frontToBackSort = true;
    float sortMaxDistance = 512.0f;
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
#include "World.h"
struct EditorState
{
    bool show = true;
    World::Entity selectedObject = entityInvalid;
    bool dirtyHierarchy = true;
    
    float4x4 cameraView;
    float4x4 cameraProj;
};
EditorState editorState;

#include "Project.h"
Project project;

#include "Loading.h"
#include "Renderer.h"
#include "UI.h"


class Engine
{
public:

    Time time;
    GPU gpu;
    Profiler profiler;
    IOs ios;
    AssetLibrary assetLibrary;
    World world;
    Renderer renderer;
    UI ui;
    MeshLoader meshLoader;
    SceneLoader sceneLoader;
    TextureLoader textureLoader;
    ShaderLoader shaderLoader;

    Systems::Player player;
    Systems::PrefabStreaming prefabStreaming;

    // live shader stats (edit -> hot-reload -> profile loop)
    float shaderStatsSecondsSinceReload = 0.0f;
    float shaderStatsCsvTimer = 0.0f;

    void On(IOs::WindowInformation window)
    {
        ZoneScoped;
        time.On();
        ios.On(window);
        world.On();
        gpu.On(&ios.window);
        assetLibrary.On();
        profiler.On();
        meshLoader.On();
        sceneLoader.On();
        textureLoader.On();
        shaderLoader.On();
        renderer.On(uint2(gpu.backBuffer.Get().GetResource()->GetDesc().Width, gpu.backBuffer.Get().GetResource()->GetDesc().Height));
        ui.On(&ios.window, gpu.device, gpu.swapChain);
        EditorWindow::Load();

        player.On(float3(project.cameraPos[0], project.cameraPos[1], project.cameraPos[2]), quaternion(project.cameraRot[0], project.cameraRot[1], project.cameraRot[2], project.cameraRot[3]));
        prefabStreaming.On();
    }

    void Loop()
    {
        while (IsRunning())
        {
            ZoneScoped;

            Time::instance->Update();
            gpu.FrameStart();
            ui.FrameStart();
            ios.ProcessMessages();

            // Prefab streaming + deferred cleanup run single-threaded at frame start, before the
            // ECS systems and the editor read the world. world.structureChanged is reset here
            // (before systems) and set by the streaming system on any load/unload; the hierarchy
            // window reads it to refresh. Running before DeferredRelease + RUN guarantees an
            // unload's entities are fully removed before the hierarchy rebuilds.
            world.structureChanged = false;
            prefabStreaming.Update(&world);
            world.DeferredRelease(); // thread this ?

            TASK(UpdateWindow);
            TASKWITHSUBFLOW(ScheduleInputs);
            TASKWITHSUBFLOW(ScheduleLoading);
            TASKWITHSUBFLOW(ScheduleWorld);
            TASKWITHSUBFLOW(ScheduleEditor);
            TASKWITHSUBFLOW(ScheduleRenderer);

            UpdateWindow.precede(ScheduleInputs);
            ScheduleInputs.precede(ScheduleWorld);
            ScheduleWorld.precede(ScheduleEditor);
            ScheduleRenderer.succeed(ScheduleEditor, ScheduleLoading);

            RUN();
            CLEAR();

            // Live shader stats: when a shader hot-reloads, restart the GPU timing window so the
            // averages reflect the NEW shader, then periodically dump shaderStats.csv. The external
            // optimize loop watches each pass's reloadId (bumps on recompile) + secondsSinceReload.
            if (options.shaderStatsCsv)
            {
                float dt = Time::instance->deltaSeconds;
                if (assetLibrary.shaderLoaded > 0)
                {
                    shaderStatsSecondsSinceReload = 0.0f;
                    profiler.ResetSamples();
                }
                else
                {
                    shaderStatsSecondsSinceReload += dt;
                }
                shaderStatsCsvTimer += dt;
                if (shaderStatsCsvTimer >= 0.5f)
                {
                    shaderStatsCsvTimer = 0.0f;
                    if (ShaderStatsCollector::instance)
                        ShaderStatsCollector::instance->WriteCsv("..\\shaderStats.csv", shaderStatsSecondsSinceReload);
                }
            }

            if (options.stepMotion)
                Sleep(250);

            FrameMark;
        }
    }

    void Off()
    {
        ZoneScoped;
        renderer.WaitFrame(); // wait previous frame
        gpu.FrameStart();
        renderer.WaitFrame(); // wait current frame

        player.Off();
        prefabStreaming.Off();

        shaderLoader.Off();
        textureLoader.Off();
        meshLoader.Off();
        sceneLoader.Off();
        ui.Off();
        profiler.Off();
        renderer.Off();
        world.Off();
        assetLibrary.Off();
        gpu.Off();
        ios.Off();
        time.Off();
    }

    void ScheduleInputs(tf::Subflow& subflow)
    {
        ZoneScoped;
        SUBTASK_(InputsUpdate, ios.Update);
        SUBTASK_(GetWindow, ios.GetWindow);

        // no link all 3 sub task paralell
    }

    void UpdateWindow()
    {
        ZoneScoped;
        if (ios.Resize())
        {
            renderer.WaitFrame();
            gpu.Resize(&ios.window);
        }
        if (ios.DropFile())
        {
            sceneLoader.Load(ios.window.dropFile);
        }
    }

    tf::Task ScheduleLoading(tf::Subflow& subflow)
    {
        ZoneScoped;

        AssetLibrary::instance->Open();
        tf::Task loadingTask = subflow.emplace([]() {AssetLibrary::instance->LoadAssets(); }).name("Loading");

        return loadingTask;
    }

    void ScheduleWorld(tf::Subflow& subflow)
    {
        world.Schedule(subflow);
    }

    void ScheduleEditor(tf::Subflow& subflow)
    {
        EditorWindow::UpdateWindows();
        /*
        for (uint i = 0; i < EditorWindow::guiWindows.size(); i++)
        {
            auto window = EditorWindow::guiWindows[i];
            //SUBTASKWINDOW(window); // no multithreading draw for imgui ?
            window->Update();
        }
        */
    }
     
    void ScheduleRenderer(tf::Subflow& subflow)
    {
        renderer.Schedule(world, subflow);
    }

    bool IsRunning()
    {
        if (ios.keys.pressed[VK_ESCAPE])
        {
            if (handlePickingWindow.isOpen)
                handlePickingWindow.Close();
            else if (fileBrowserWindow.isOpen)
                fileBrowserWindow.Close();
            else if (editorState.selectedObject != entityInvalid)
                editorState.selectedObject = entityInvalid;
            else
                return false;
        }
        return true;
    }
};

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    // call that each time to force the correctness of component metadata ?
    //ParseCpp("G:\\Work\\Dev\\SeeD\\SeeD\\src\\World.h");

    // The project (asset dirs + window/render settings + editor layout) must be loaded BEFORE the
    // window/GPU are created, since it supplies the window settings.
    project.Load(lpCmdLine ? String(lpCmdLine) : String(""));

    IOs::WindowInformation window;
    window.usevSync = project.vsync;
#ifdef _DEBUG
    window.usevSync = true;
#endif
    window.fullScreen = project.fullscreen;
    window.windowInstance = hInstance;
    window.windowResolution = uint2(project.windowWidth, project.windowHeight);

    Engine engine;
    engine.On(window);

    options.frontToBackSort = project.frontToBackSort;
    options.sortMaxDistance = project.sortMaxDistance;

    if (!project.startupScene.empty())
        World::instance->Load(project.SceneAbs(project.startupScene));

    engine.Loop();
    engine.Off();

    return 0;
}