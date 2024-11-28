
#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <vector>
#include <unordered_map>

#include "../../Third/hlslpp-master/include/hlsl++.h"
using namespace hlslpp;

#define NAMEOF(name) #name


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
    World world;
    Renderer renderer;
    UI ui;
    AssetLoader assetLoader;
    TextureLoader textureLoader;
    ShaderLoader shaderLoader;

    void Start(IOs::WindowInformation window)
    {
        time.Start();
        ios.Start(window);
        gpu.Start(&ios.window);
        profiler.Start();
        renderer.Start(&ios.window);
        ui.Start(&ios.window, gpu.device, gpu.swapChain);
        assetLoader.Start();
        textureLoader.Start();
        shaderLoader.Start();
    }

    void Loop()
    {
        while (IsRunning())
        {
            ZoneScoped;

            gpu.FrameStart();
            ui.FrameStart();
            ios.ProcessMessages();

            TASKWITHSUBFLOW(ScheduleInputs);
            TASK(UpdateWindow);
            TASKWITHSUBFLOW(ScheduleWorld);
            TASKWITHSUBFLOW(ScheduleEditor);
            TASKWITHSUBFLOW(ScheduleRenderer);

            ScheduleInputs.precede(UpdateWindow);
            UpdateWindow.precede(ScheduleWorld);
            ScheduleWorld.precede(ScheduleEditor);
            ScheduleEditor.precede(ScheduleRenderer);

            RUN();
            CLEAR();

            FrameMark;
        }
    }

    void Stop()
    {
        renderer.WaitFrame(); // wait previous frame
        gpu.FrameStart();
        renderer.WaitFrame(); // wait current frame

        shaderLoader.Stop();
        textureLoader.Stop();
        assetLoader.Stop();
        ui.Stop();
        renderer.Stop();
        profiler.Stop();
        gpu.Stop();
        ios.Stop();
        time.Stop();
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
            Load();
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
        renderer.Schedule(&world, subflow);
    }

    void Load()
    {
        world.Add();
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
    window.fullScreen = false;
    window.windowInstance = hInstance;
    window.windowResolution = int2(1600, 900);
    //window.windowResolution *= 2.f;

    Engine engine;
    engine.Start(window);
    engine.Loop();
    engine.Stop();

    return 0;
}