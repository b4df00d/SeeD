
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

            TASK(UpdateWindow);
            TASKWITHSUBFLOW(ScheduleInputs);
            TASKWITHSUBFLOW(ScheduleLoading);
            TASKWITHSUBFLOW(ScheduleWorld);
            TASKWITHSUBFLOW(ScheduleEditor);
            TASKWITHSUBFLOW(ScheduleRenderer);
            TASK_(PhysicsStep, physics.Step);

            UpdateWindow.precede(ScheduleInputs);
            ScheduleInputs.precede(ScheduleWorld);
            PhysicsStep.succeed(ScheduleWorld);
            ScheduleWorld.succeed(ScheduleLoading);
            ScheduleWorld.precede(ScheduleEditor);
            ScheduleRenderer.succeed(ScheduleEditor, ScheduleLoading);

            RUN();
            CLEAR();

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

    void ScheduleInputs(tf::Subflow& subflow)
    {
        ZoneScoped;
        SUBTASK_(InputsUpdate, ios.Update);
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

    tf::Task ScheduleLoading(tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task loadingTask = subflow.emplace([]() {AssetLibrary::instance->Open(); AssetLibrary::instance->LoadAssets(); }).name("Loading");

        return loadingTask;
    }

    void ScheduleWorld(tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task prefabTask = subflow.emplace([this]() { prefabStreaming.Update(&world); }).name("PrefabStreaming");
        tf::Task terrainTask = subflow.emplace([this]() { terrainStreaming.Update(&world); }).name("TerrainStreaming");
        tf::Task deferredReleaseTask = subflow.emplace([this]() { world.DeferredRelease(); }).name("DeferredRelease"); // thread this ?
        tf::Task physicsSyncTask = subflow.emplace([this]() { physics.SyncAndReadback(&world); }).name("PhysicsSyncReadback");
        prefabTask.precede(terrainTask);
        terrainTask.precede(physicsSyncTask);
        physicsSyncTask.precede(deferredReleaseTask);

        world.Schedule(subflow, deferredReleaseTask);
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