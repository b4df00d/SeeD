#pragma once
// Offline render capture, driven by the [calibration] section of the .seedproj.
//
// A project with capture entries renders each one and then exits; a project
// without them never touches any of this. There is deliberately no command-line
// flag and no calibration-specific enforcement of render settings -- everything
// a capture needs (resolution, upscaling, vsync) is already a project setting.
//
// Per entry: unload whatever is loaded, load the entry's scene, wait for
// streaming to settle, warm up, grab one frame, write it. One frame is enough
// because DLSS-RR does the denoising; the warmup is what matters, since DLSS-RR,
// the ReSTIR reservoirs and the SHARC cache all converge from scratch.
//
// The captured image is linear HDR from before tonemapping, which is the whole
// point: it can be compared against a path tracer without a display transform in
// the way. The tonemapped frame is written next to it (<output>_display.exr,
// sRGB-encoded 0-1 values) so the display transform can be compared as well.
// See SeeD/tools/calibration/ for the Blender side and the scoring.
//
// Everything this needs is handed over by Project::Setup, so at run time it reads
// no globals of its own: the entries already carry absolute paths, and Update()
// takes the renderer rather than the Engine. That is what lets Engine hold one by
// value, since this header is included before Engine.h.

class Calibration
{
public:
    // Parsed from the project file, then handed over with paths already resolved.
    struct CaptureEntry
    {
        String name;
        String scene;        // .seed, or .gltf/.glb/.fbx to import
        String output;       // .exr / .hdr / .dds / .png
        bool directOnly = false;
        uint warmupFrames = 120;
    };

    // Exit codes, read by run.ps1.
    static const int exitOk = 0;
    static const int exitAssetTimeout = 3;
    static const int exitCaptureFailed = 4;

    // Filled by Project::Setup before Engine::On().
    std::vector<CaptureEntry> captures;
    uint expectedWidth = 0;    // what the project asked the window for; a smaller
    uint expectedHeight = 0;   // client area silently invalidates the comparison
    String logPath;

    void On()
    {
        if (!Active()) return;
        // IOs rebinds stdout to its own console, so a parent process cannot read
        // it. The log file is how run.ps1 reports, and the record of what a
        // capture actually did.
        std::ofstream truncate((std::string)logPath, std::ios::trunc);
    }

    bool Active() const { return !captures.empty(); }
    bool Finished() const { return Active() && state == State::Done; }
    int ExitCode() const { return exitCode; }

    void Update(Renderer& renderer)
    {
        if (!Active() || Finished()) return;

        framesInState++;

        switch (state)
        {
        case State::Start:
            Log("[calibration] {} capture(s) queued", (uint)captures.size());
            Enter(State::Unload);
            break;

        case State::Unload:
            // Clear() defers the entity release, so give it a couple of frames to
            // drain before the next scene arrives. Running it here, after RUN()
            // has joined, keeps it single threaded and outside the task graph.
            if (framesInState == 1)
                World::instance->Clear();
            else if (framesInState > unloadFrames)
            {
                if (entryIndex >= captures.size())
                    Finish(exitCode);
                else
                    Enter(State::LoadScene);
            }
            break;

        case State::LoadScene:
        {
            auto& entry = captures[entryIndex];
            Log("[calibration] '{}' loading {}{}", entry.name.c_str(), entry.scene.c_str(),
                entry.directOnly ? " (direct only)" : "");
            World::instance->Load(entry.scene);
            renderer.mainView.raytracingContext.rtParameters.directOnly = entry.directOnly;
            idleFrames = 0;
            Enter(State::WaitAssets);
            break;
        }

        case State::WaitAssets:
            // One quiet frame proves nothing -- meshes and textures stream in over
            // several -- so require a run of them.
            if (AssetLibrary::instance->IsIdle())
                idleFrames++;
            else
                idleFrames = 0;

            if (idleFrames >= idleFramesRequired)
            {
                Log("[calibration] '{}' assets settled after {} frames",
                    captures[entryIndex].name.c_str(), framesInState);
                Enter(State::Warmup);
            }
            else if (framesInState > assetTimeoutFrames)
            {
                Log("[calibration] '{}' timed out waiting for assets after {} frames",
                    captures[entryIndex].name.c_str(), framesInState);
                Finish(exitAssetTimeout);
            }
            break;

        case State::Warmup:
            if (framesInState > captures[entryIndex].warmupFrames)
                Enter(State::Capture);
            break;

        case State::Capture:
        {
            auto& entry = captures[entryIndex];
            if (!CaptureTo(renderer, entry.name, entry.output))
                exitCode = exitCaptureFailed; // keep going; the run reports at the end
            entryIndex++;
            Enter(State::Unload);
            break;
        }

        case State::Done:
            break;
        }
    }

private:
    enum class State { Start, Unload, LoadScene, WaitAssets, Warmup, Capture, Done };

    static const uint idleFramesRequired = 30;
    static const uint assetTimeoutFrames = 3600;
    static const uint unloadFrames = 3;

    State state = State::Start;
    uint entryIndex = 0;
    uint framesInState = 0;
    uint idleFrames = 0;
    int exitCode = exitOk;

    // Mirrors IOs::Log's formatting so call sites read the same, and appends to
    // the capture log.
    template<typename... Args>
    void Log(std::string_view fmt, Args&&... args)
    {
        std::string message = std::vformat(fmt, std::make_format_args(args...));
        IOs::Log(message);
        if (!logPath.empty())
        {
            std::ofstream f((std::string)logPath, std::ios::app);
            if (f.is_open()) f << message << "\n";
        }
    }

    void Enter(State next)
    {
        state = next;
        framesInState = 0;
    }

    void Finish(int code)
    {
        exitCode = code;
        state = State::Done;
        Log("[calibration] done, exit code {}", code);
    }

    // What the renderer will actually light with, in world space. Worth recording
    // for every capture: a light's direction comes from its transform rather than
    // from anything the importer logs, so an orientation that survived the glTF
    // conversion wrongly is otherwise invisible until the numbers come out odd.
    void LogLights()
    {
        World& world = *World::instance;
        uint queryIndex = world.Query(Components::Light::mask, 0, true);
        auto& lights = world.frameQueries[queryIndex];
        for (uint i = 0; i < (uint)lights.size(); i++)
        {
            auto& light = lights[i].Get<Components::Light>();
            float4x4 m = ComputeWorldMatrix(lights[i]);
            float3 dir = normalize(float3(m[2].xyz));   // UpdateLights uses this same +Z axis
            float3 pos = float3(m[3].xyz);
            Log("[calibration]   light type {} intensity {:.4f} colour {:.3f} {:.3f} {:.3f}"
                " dir {:.4f} {:.4f} {:.4f} pos {:.3f} {:.3f} {:.3f} size {:.5f} range {:.3f}",
                (uint)light.type, (float)light.color.w,
                (float)light.color.x, (float)light.color.y, (float)light.color.z,
                (float)dir.x, (float)dir.y, (float)dir.z,
                (float)pos.x, (float)pos.y, (float)pos.z,
                light.size, light.range);
        }
    }

    bool CaptureTo(Renderer& renderer, const String& name, const String& outPath)
    {
        MainView& view = renderer.mainView;

        // With DLSS the final HDR image is the upscaled one; PostProcess reads it
        // instead of `lighted`. Both are R16G16B16A16_FLOAT and both sit in UAV
        // state at rest, and DLSS-RR outputs linear HDR, so either way this lands
        // before tonemapping.
        const bool upscaling = (view.upscaling == HLSL::Upscaling::dlss
                                || view.upscaling == HLSL::Upscaling::dlssd);
        const char* resourceName = upscaling ? "upscaled" : "lighted";
        // Upscaling 0=none 1=taa 2=dlss 3=dlssd. A render resolution below the
        // display resolution is the proof DLSS actually engaged. The camera count
        // matters just as much: the renderer draws through the first camera the
        // query returns, so zero cameras means every frame used a zero matrix.
        Log("[calibration] '{}' capturing '{}' (upscaling {}, render {}x{} -> display {}x{}, {} camera(s))",
            name.c_str(), resourceName, (uint)view.upscaling,
            (uint)view.renderResolution.x, (uint)view.renderResolution.y,
            (uint)view.displayResolution.x, (uint)view.displayResolution.y,
            World::instance->CountQuery(Components::Camera::mask, 0));

        LogLights();

        renderer.WaitFrame();

        if (!WriteResource(view, name, resourceName, outPath))
            return false;

        // The tonemapped frame (PostProcess output, what gets copied to the backbuffer).
        std::filesystem::path display((std::string)outPath);
        display.replace_filename(display.stem().string() + "_display" + display.extension().string());
        return WriteResource(view, name, "postProcessed", String(display.string().c_str()));
    }

    // Both resources sit in UNORDERED_ACCESS at rest. Anything that isn't float is converted, so
    // an R8G8B8A8_UNORM target comes out as its encoded 0-1 values.
    bool WriteResource(MainView& view, const String& name, const char* resourceName, const String& outPath)
    {
        // GetRegisteredResource default-constructs on a miss, so check the D3D
        // resource rather than trusting the lookup.
        Resource& res = view.GetRegisteredResource(String(resourceName));
        ID3D12Resource* source = res.GetResource();
        if (source == nullptr)
        {
            Log("[calibration] resource '{}' is not registered", resourceName);
            return false;
        }

        DirectX::ScratchImage captured;
        HRESULT hr = DirectX::CaptureTexture(GPU::instance->graphicQueue, source, false, captured,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (FAILED(hr))
        {
            Log("[calibration] CaptureTexture failed (hr 0x{:08x})", (uint)hr);
            return false;
        }

        const DirectX::Image* image = captured.GetImage(0, 0, 0);
        DirectX::ScratchImage converted;
        if (image->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
        {
            hr = DirectX::Convert(*image, DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_DEFAULT,
                                  DirectX::TEX_THRESHOLD_DEFAULT, converted);
            if (FAILED(hr))
            {
                Log("[calibration] converting the captured image failed (hr 0x{:08x})", (uint)hr);
                return false;
            }
            image = converted.GetImage(0, 0, 0);
        }

        const uint width = (uint)image->width;
        const uint height = (uint)image->height;
        if (expectedWidth != 0 && (width != expectedWidth || height != expectedHeight))
        {
            // Worth shouting about: the reference was rendered at the requested
            // size, so a smaller window silently invalidates the comparison.
            Log("[calibration] captured {}x{} but the project asks for {}x{}",
                width, height, expectedWidth, expectedHeight);
            exitCode = exitCaptureFailed;
        }

        // ImageWriter wants tightly packed rows; the captured image has its own pitch.
        std::vector<float> pixels((size_t)width * height * 4);
        for (uint y = 0; y < height; y++)
        {
            memcpy(pixels.data() + (size_t)y * width * 4,
                   image->pixels + (size_t)y * image->rowPitch,
                   (size_t)width * 4 * sizeof(float));
        }

        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path((std::string)outPath).parent_path(), ec);

        if (!ImageWriter::WriteImage(outPath, width, height, pixels.data()))
            return false;

        Log("[calibration] '{}' wrote {} ({}x{})", name.c_str(), outPath.c_str(), width, height);
        return true;
    }
};
