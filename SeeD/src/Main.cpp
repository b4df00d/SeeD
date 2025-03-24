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

#include "Time.h"
#include "IO.h"
#include "Tasks.h"
#include "World.h"
struct EditorState
{
    bool show;
    bool shaderReload =
#ifdef _DEBUG
        true;
#else
        false;
#endif
    World::Entity selectedObject = entityInvalid;
    bool dirtyHierarchy = true;
    /*
    ECS::Entity lastSelectedObject = { 0 };
    uint activeWorld = 0;
    ImGui::FileBrowser fileDialog;
    float gizmoScale = 0.1f;
    bool showEditorCamera;
    ECS::Entity browseEntity = { 0 };
    int browseSavedWorld = -1;
    char filter[512];
    */
};
EditorState editorState;
#include "GPU.h"
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
        renderer.On(ios.window);
        ui.On(&ios.window, gpu.device, gpu.swapChain);
        meshLoader.On();
        sceneLoader.On();
        textureLoader.On();
        shaderLoader.On();

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

            if (options.stepMotion)
                Sleep(1000);

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
    }

    void ScheduleInputs(tf::Subflow& subflow)
    {
        ZoneScoped;
        SUBTASK_(InputsUpdate, ios.Update);
        SUBTASK_(GetMouse, ios.GetMouse);
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
        for (uint i = 0; i < EditorWindow::guiWindows.size(); i++)
        {
            auto window = EditorWindow::guiWindows[i];
            //SUBTASKWINDOW(window); // no multithreading draw for imgui ?
            window->Update();
        }
    }
     
    void ScheduleRenderer(tf::Subflow& subflow)
    {
        renderer.Schedule(world, subflow);
    }

    bool IsRunning()
    {
        return !ios.keys.down[VK_ESCAPE];
    }
};

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    IOs::WindowInformation window;
    window.usevSync = false;
#ifdef _DEBUG
    window.usevSync = true;
#endif
    window.fullScreen = false;
    window.windowInstance = hInstance;
    window.windowResolution = uint2(float2(1600.0f, 900.0f) * 1.5f);
    //window.windowResolution *= 2.f;

    Engine engine;
    engine.On(window);
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\places\\5v5GameMap.fbx"); 
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Vehicules\\bus scifi.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Human\\the-queen-of-swords\\the queen of swords.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\_Environment.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\toko.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Bistro\\BistroInterior.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Provence\\Provence.gltf");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\tokyo\\source\\Export.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Halo.fbx");
    engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\Stronghold.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Places\\BOURDON-V01.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Objects\\primitives.fbx");
    //engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\places\\caldera-main\\map_source\\prefabs\\br\\wz_vg\\mp_wz_island\\commercial\\hotel_01.usd");
    engine.Loop();
    engine.Off();

    return 0;
}