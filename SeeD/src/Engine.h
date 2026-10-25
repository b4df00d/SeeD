
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
    OMMBaker ommBaker;

    Systems::Player player;
    Systems::Driver driver;
    Systems::PrefabStreaming prefabStreaming;
    Systems::TerrainStreaming terrainStreaming;
    Physics physics;
    Calibration calibration;

    // the frame graph is built once and re-run (see Tasks.h): these are its persistent sub-graphs
    tf::Taskflow worldFlow;
    tf::Taskflow rendererFlow;
    size_t builtScheduleKey = ~size_t(0);

    void On()
    {
        ZoneScoped;
        time.On();
        ios.On();
        world.On();
        gpu.On(&ios.window);
        assetLibrary.On();
        profiler.On();
        meshLoader.On();
        sceneLoader.On();
        textureLoader.On();
        shaderLoader.On();
        ommBaker.On();
        renderer.On(uint2(gpu.backBuffer.Get().GetResource()->GetDesc().Width, gpu.backBuffer.Get().GetResource()->GetDesc().Height));
        ui.On(&ios.window, gpu.device, gpu.swapChain);

        if (options.playerSystem) player.On();
        driver.On();
        prefabStreaming.On();
        terrainStreaming.On();
        physics.On();
        calibration.On();
    }

    void Loop()
    {
        while (IsRunning())
        {
            ZoneScoped;

            world.structureChanged = false;

            Time::instance->Update();
            gpu.FrameStart();
            ui.FrameStart();
            ios.ProcessMessages();

            if (ScheduleKey() != builtScheduleKey)
                BuildSchedule();

            RUN();

            if (ShaderStatsCollector::instance)
                ShaderStatsCollector::instance->Update();

            calibration.Update(renderer);

            if (options.stepMotion)
                Sleep(100);

            FrameMark;
        }
    }

    void Off()
    {
        ZoneScoped;
        renderer.WaitFrame(); // wait previous frame
        gpu.FrameStart();
        renderer.WaitFrame(); // wait current frame

        CLEAR(); // its module tasks point at worldFlow/rendererFlow

        physics.Off();
        if (options.playerSystem) player.Off();
        driver.Off();
        prefabStreaming.Off();
        terrainStreaming.Off();

        shaderLoader.Off();
        ommBaker.Off();
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

    // Everything the frame graph's shape depends on. A system's task reads systems[i] at run time,
    // so only the count matters, not which systems they are.
    size_t ScheduleKey()
    {
        return world.systems.size();
    }

    void BuildSchedule()
    {
        ZoneScoped;
        CLEAR();
        worldFlow.clear();
        rendererFlow.clear();

        ScheduleWorld(worldFlow);
        renderer.Schedule(world, rendererFlow);

        TASK(UpdateWindow);
        TASK_(Inputs, ios.Update);
        tf::Task LoadAssets = taskflow.emplace([]() { AssetLibrary::instance->Open(); AssetLibrary::instance->LoadAssets(); }).name("Loading");
        MODULE(WorldModule, worldFlow);
        tf::Task EditorWindows = taskflow.emplace([]() { EditorWindow::UpdateWindows(); }).name("Editor");
        MODULE(RendererModule, rendererFlow);
        TASK_(PhysicsStep, physics.Step);

        UpdateWindow.precede(Inputs);
        Inputs.precede(WorldModule);
        PhysicsStep.succeed(WorldModule);
        WorldModule.succeed(LoadAssets);
        WorldModule.precede(EditorWindows);
        RendererModule.succeed(EditorWindows, LoadAssets);

        builtScheduleKey = ScheduleKey();
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
            world.Load(ios.window.dropFile);
        }
    }

    void ScheduleWorld(tf::FlowBuilder& flow)
    {
        ZoneScoped;

        tf::Task frameStartTask = flow.emplace([this]() { world.FrameStart(); }).name("WorldFrameStart");
        tf::Task prefabTask = flow.emplace([this]() { prefabStreaming.Update(&world); }).name("PrefabStreaming");
        tf::Task terrainTask = flow.emplace([this]() { terrainStreaming.Update(&world); }).name("TerrainStreaming");
        tf::Task deferredReleaseTask = flow.emplace([this]() { world.DeferredRelease(); }).name("DeferredRelease"); // thread this ?
        tf::Task physicsSyncTask = flow.emplace([this]() { physics.SyncAndReadback(&world); }).name("PhysicsSyncReadback");
        frameStartTask.precede(prefabTask);
        prefabTask.precede(terrainTask);
        terrainTask.precede(physicsSyncTask);
        physicsSyncTask.precede(deferredReleaseTask);

        world.Schedule(flow, deferredReleaseTask);
    }

    bool IsRunning()
    {
        if (calibration.Finished())
            return false; // every [calibration] capture is written; quit

        bool consumed = false;
        for (auto& window : EditorWindow::guiWindows)
        {
            if (window->isOpen && window->KeyEvent()) { consumed = true; break; }
        }

        if (!consumed && ios.keys.pressed[VK_ESCAPE])
        {
            if (editorState.selectedObject != entityInvalid)
                editorState.selectedObject = entityInvalid;
            else
                return false; // nothing left to dismiss -> quit
        }
        return true;
    }
};