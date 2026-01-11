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


struct Options
{
    bool stopFrustumUpdate;
    bool stopBufferUpload;
    bool stepMotion;
    bool shaderReload;

    enum class DebugMode
    {
        none,
        ray,
        boundingSphere,
    };
    DebugMode debugMode;

    enum class DebugDraw
    {
        none,
        albedo,
        lighting,
        GIprobes,
    };
    DebugDraw debugDraw;
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

    void On(IOs::WindowInformation window)
    {
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

        player.On();
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

            world.DeferredRelease(); // thread this ?

            if (options.stepMotion)
                Sleep(500);

            FrameMark;
        }
    }

    void Off()
    {
        renderer.WaitFrame(); // wait previous frame
        gpu.FrameStart();
        renderer.WaitFrame(); // wait current frame

        player.Off();

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
        EditorWindow::Save();
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

    IOs::WindowInformation window;
    window.usevSync = false;
#ifdef _DEBUG
    window.usevSync = true;
#endif
    window.fullScreen = false;
    window.windowInstance = hInstance;
    window.windowResolution = uint2(float2(1600.0f, 900.0f) * 1.75f);

    Engine engine;
    engine.On(window);

    World::instance->Load("Save.seed");

    //engine.assetLibrary.importPath = "E:\\Work\\Dev\\EngineAssets\\";
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Vehicules\\bus scifi.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Mario1\\scene.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Stronghold.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Japanese restaurant\\source\\Inakaya_Cycles2.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\bridge\\source\\nature-and-cyvilization.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Cabin\\Rural_Cabins.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\_Environment.fbx");
    
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\places\\5v5GameMap.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Human\\the-queen-of-swords\\the queen of swords.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\toko.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Blokcing_obj.obj");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\GTA.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Bistro\\BistroInterior.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Provence\\Provence.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\tokyo\\source\\Export.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\WildWest\\WildWest2.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Halo.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\BOURDON-V01.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Objects\\primitives.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\places\\caldera-main\\map_source\\prefabs\\br\\wz_vg\\mp_wz_island\\commercial\\hotel_01.usd");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\appart-vincent-03-v03.fbx");


    //engine.assetLibrary.importPath = "E:\\Work\\Dev\\EngineAssets2\\";
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets2\\BrutalistLevelKit\\brutalist.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets2\\FantasticVillage\\map_village_day.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets2\\Cambodia\\TemplesOfCambodia_01_01_Exterior.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets2\\Bazaar\\LV_Bazaar.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets2\\ScienceLab\\ScieneLab.gltf");


    engine.assetLibrary.importPath = "E:\\Work\\Dev\\EngineAssets3\\";
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets3\\SpaceJunkyard_NantStudios2.gltf");

    engine.Loop();
    engine.Off();

    return 0;
}