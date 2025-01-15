//#undef NDEBUG
#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <vector>
#include <unordered_map>

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

struct EditorState
{
    bool show;
    bool shaderReload =
#ifdef _DEBUG
        true;
#else
        false;
#endif
    /*
    ECS::Entity selectedObject = { 0 };
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

#include "Containers.h"
#include "String.h"

#include "Time.h"
#include "IO.h"
#include "Tasks.h"
#include "World.h"
#include "GPU.h"
#include "Loading.h"
#include "UI.h"
#include "Renderer.h"


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
        assetLibrary.On();
        world.On();
        gpu.On(&ios.window);
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
            TASKWITHSUBFLOW(ScheduleWorld);
            TASKWITHSUBFLOW(ScheduleEditor);
            TASKWITHSUBFLOW(ScheduleRenderer);

            UpdateWindow.precede(ScheduleInputs);
            ScheduleInputs.precede(ScheduleWorld);
            ScheduleWorld.precede(ScheduleEditor);
            ScheduleEditor.precede(ScheduleRenderer);

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
    window.windowResolution = uint2(1600, 900);
    //window.windowResolution *= 2.f;

    Engine engine;
    engine.On(window);
    engine.sceneLoader.Load("E:\\Work\\Dev\\EngineAssets\\Vehicules\\buggy.fbx");
    engine.Loop();
    engine.Off();

    return 0;
}