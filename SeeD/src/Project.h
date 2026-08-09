#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

class Project
{
public:
    static Project* instance;

    Engine* enginePtr = nullptr; // set by Setup(); Save() reads the live engine state through it

    String filePath;     // absolute path to project.seedproj
    String projectDir;   // absolute dir containing it, trailing separator
    String imguiIniPath; // absolute; owned here so ImGui's io.IniFilename can point at it

    // [settings]
    String sourceDir = "Source";     // relative to projectDir
    String cacheDir = "Cache";       // relative to projectDir
    String startupScene = "";        // relative .seed to load on open (optional)
    uint windowWidth = 2800;
    uint windowHeight = 1575;
    bool vsync = false;
    bool fullscreen = false;
    bool frontToBackSort = true;
    float sortMaxDistance = 512.0f;
    float lodDistanceMultiplier = 0.05f;
    float distanceCullingValue = 6.0f;
    float distanceCullingValueRT = 6.0f;

    // Rendering window -> "Ray Tracing" section
    uint maxFrameFilteringCount = 1;
    float spacialRadius = 64.0f;
    uint spacialSampleCount = 16;
    float reservoirRandBias = 0.0f;
    float reservoirSpacialRandBias = 0.2f;
    float SHARCSceneScale = 20.0f;
    float SHARCRadianceScale = 1.0f;
    float SHARCRoughnessThreshold = 0.33f;
    uint SHARCSamplesPerPixel = 1;
    uint SHARCAccumulationFrameNum = 128;

    // Rendering window -> "Upscaling" section; raw ints here so Project.h stays decoupled from
    // the HLSL/NGX headers. upscalingTechnique matches HLSL::Upscaling (none=0 taa=1 dlss=2 dlssd=3),
    // dlssQuality matches NVSDK_NGX_PerfQuality_Value, dlssPreset/dlssdPreset match the SR/RR preset enums.
    uint upscalingTechnique = 1;
    int dlssQuality = 1;
    int dlssPreset = 0;
    int dlssdPreset = 0;

    // saved player camera (fed to Systems::Player on load). rotation is a quaternion (xyzw).
    bool hasCamera = false;
    float cameraPos[3] = { 0, 1, -2 };
    float cameraRot[4] = { 0, 0, 0, 1 };

    // [uilayout] : editor window open-states (replaces UILayout.txt)
    struct WindowState { String name; bool open; };
    std::vector<WindowState> windows;

    String SourceDirAbs() { return projectDir + sourceDir + "\\"; } // trailing sep: loaders do format("{}{}.mesh", path, id)
    String CacheDirAbs() { return projectDir + cacheDir + "\\"; }
    String SceneAbs(String rel) { return projectDir + rel; }

    bool WindowOpen(String name, bool def)
    {
        for (auto& w : windows) if (w.name == name) return w.open;
        return def;
    }
    void SetWindowOpen(String name, bool open)
    {
        for (auto& w : windows) if (w.name == name) { w.open = open; return; }
        windows.push_back({ name, open });
    }

    bool Load(String pathOrDir)
    {
        instance = this;
        namespace fs = std::filesystem;

        std::string arg = Trim((std::string)pathOrDir);

        fs::path file;
        std::error_code ec;
        if (arg.empty())
            file = fs::current_path() / "project.seedproj";
        else
        {
            fs::path p = arg;
            if (fs::is_directory(p, ec)) file = p / "project.seedproj";
            else file = p;
        }
        file = fs::absolute(file);
        filePath = file.string();
        projectDir = file.parent_path().string() + "\\";
        imguiIniPath = projectDir + "imgui.ini";

        bool parsed = Parse(file.string());

        fs::create_directories((std::string)SourceDirAbs(), ec);
        fs::create_directories((std::string)CacheDirAbs(), ec);
        return parsed;
    }

    void Save()
    {
        windows.clear();
        for (EditorWindow* ew : EditorWindow::guiWindows)
        {
            windows.push_back({ ew->name, ew->isOpen });
        }

        std::ofstream f(filePath);
        if (!f.is_open()) { IOs::Log("Fail to write project {}", filePath.c_str()); return; }
        f << "SEEDPROJ 1\n";
        f << "[settings]\n";
        f << "sourceDir " << sourceDir << "\n";
        f << "cacheDir " << cacheDir << "\n";
        f << "startupScene " << startupScene << "\n";

        auto& window = enginePtr->ios.window;
        f << "windowWidth " << window.windowResolution.x << "\n";
        f << "windowHeight " << window.windowResolution.y << "\n";
        f << "vsync " << (window.usevSync ? 1 : 0) << "\n";
        f << "fullscreen " << (window.fullScreen ? 1 : 0) << "\n";

        f << "frontToBackSort " << (options.frontToBackSort ? 1 : 0) << "\n";
        f << "sortMaxDistance " << options.sortMaxDistance << "\n";
        f << "lodDistanceMultiplier " << options.lodDistanceMultiplier << "\n";
        f << "distanceCullingValue " << options.distanceCullingValue << "\n";
        f << "distanceCullingValueRT " << options.distanceCullingValueRT << "\n";

        auto& rt = enginePtr->renderer.mainView.raytracingContext.rtParameters;
        f << "maxFrameFilteringCount " << rt.maxFrameFilteringCount << "\n";
        f << "spacialRadius " << rt.spacialRadius << "\n";
        f << "spacialSampleCount " << rt.spacialSampleCount << "\n";
        f << "reservoirRandBias " << rt.reservoirRandBias << "\n";
        f << "reservoirSpacialRandBias " << rt.reservoirSpacialRandBias << "\n";
        f << "SHARCSceneScale " << rt.SHARCSceneScale << "\n";
        f << "SHARCRadianceScale " << rt.SHARCRadianceScale << "\n";
        f << "SHARCRoughnessThreshold " << rt.SHARCRoughnessThreshold << "\n";
        f << "SHARCSamplesPerPixel " << rt.SHARCSamplesPerPixel << "\n";
        f << "SHARCAccumulationFrameNum " << rt.SHARCAccumulationFrameNum << "\n";

        auto& dlss = enginePtr->renderer.mainView.dlss;
        f << "upscalingTechnique " << (uint)enginePtr->renderer.mainView.upscaling << "\n";
        f << "dlssQuality " << (int)dlss.requestedQuality << "\n";
        f << "dlssPreset " << (int)dlss.dlssPreset << "\n";
        f << "dlssdPreset " << (int)dlss.dlssdPreset << "\n";

        World::Entity& cam = enginePtr->player.camera;
        if (cam.IsValid() && cam.Has<Components::Transform>())
        {
            auto& trans = cam.Get<Components::Transform>();
            store(trans.position, cameraPos);
            for (uint i = 0; i < 4; i++) cameraRot[i] = trans.rotation.f32[i];
            hasCamera = true;
        }
        f << "camera " << cameraPos[0] << " " << cameraPos[1] << " " << cameraPos[2]
          << " " << cameraRot[0] << " " << cameraRot[1] << " " << cameraRot[2] << " " << cameraRot[3] << "\n";

        f << "[uilayout]\n";
        for (auto& w : windows) f << w.name << " " << (w.open ? 1 : 0) << "\n";

        IOs::Log("Project saved to {}", filePath.c_str());
    }

    void Setup(Engine& engine)
    {
        enginePtr = &engine;

        auto& window = engine.ios.window;
        window.usevSync = vsync;
        window.fullScreen = fullscreen;
        window.windowResolution = uint2(windowWidth, windowHeight);

        options.frontToBackSort = frontToBackSort;
        options.sortMaxDistance = sortMaxDistance;
        options.lodDistanceMultiplier = lodDistanceMultiplier;
        options.distanceCullingValue = distanceCullingValue;
        options.distanceCullingValueRT = distanceCullingValueRT;

        // Rendering window "Ray Tracing" + "Upscaling" sections live directly on the view/DLSS
        // objects rather than in Options, so restore them there instead.
        auto& rt = engine.renderer.mainView.raytracingContext.rtParameters;
        rt.maxFrameFilteringCount = maxFrameFilteringCount;
        rt.spacialRadius = spacialRadius;
        rt.spacialSampleCount = spacialSampleCount;
        rt.reservoirRandBias = reservoirRandBias;
        rt.reservoirSpacialRandBias = reservoirSpacialRandBias;
        rt.SHARCSceneScale = SHARCSceneScale;
        rt.SHARCRadianceScale = SHARCRadianceScale;
        rt.SHARCRoughnessThreshold = SHARCRoughnessThreshold;
        rt.SHARCSamplesPerPixel = SHARCSamplesPerPixel;
        rt.SHARCAccumulationFrameNum = SHARCAccumulationFrameNum;

        DLSS& dlss = engine.renderer.mainView.dlss;
        engine.renderer.mainView.upscaling = (HLSL::Upscaling)upscalingTechnique;
        dlss.dlssPreset = (NVSDK_NGX_DLSS_Hint_Render_Preset)dlssPreset;
        dlss.dlssdPreset = (NVSDK_NGX_RayReconstruction_Hint_Render_Preset)dlssdPreset;
        dlss.requestedQuality = (NVSDK_NGX_PerfQuality_Value)dlssQuality;
        dlss.qualityChangePending = (dlss.requestedQuality != dlss.perf_quality); // triggers the existing rebuild path if it differs from the just-created default

        AssetLibrary& al = engine.assetLibrary;
        al.assetsPath = Project::instance->CacheDirAbs();
        al.importPath = Project::instance->SourceDirAbs();

        Systems::Player& player = engine.player;
        player.position = float3(cameraPos[0], cameraPos[1], cameraPos[2]);
        player.rotation = quaternion(cameraRot[0], cameraRot[1], cameraRot[2], cameraRot[3]);

        UI& ui = engine.ui;
        ui.imguiIniPath = imguiIniPath;
        for (EditorWindow* ew : EditorWindow::guiWindows)
        {
            for (const auto& w : windows)
            {
                if (ew->name == w.name)
                {
                    ew->isOpen = w.open;
                    break;
                }
            }
        }
    }

private:
    static std::string Trim(std::string s)
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '"' || s.front() == '\t')) s.erase(s.begin());
        while (!s.empty() && (s.back() == ' ' || s.back() == '"' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
        return s;
    }

    bool Parse(std::string path)
    {
        std::ifstream f(path);
        if (!f.is_open()) return false; // no file yet -> defaults; created on first Save
        std::string line, section;
        while (std::getline(f, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (line.empty()) continue;
            if (line.rfind("SEEDPROJ", 0) == 0) continue; // magic/version line
            if (line.front() == '[') { section = line.substr(1, line.find(']') - 1); continue; }

            if (section == "settings")
            {
                size_t sp = line.find(' ');
                if (sp == std::string::npos) continue;
                std::string key = line.substr(0, sp);
                std::string val = line.substr(sp + 1);
                if (key == "sourceDir") sourceDir = val;
                else if (key == "cacheDir") cacheDir = val;
                else if (key == "startupScene") startupScene = val;
                else if (key == "windowWidth") windowWidth = (uint)std::stoul(val);
                else if (key == "windowHeight") windowHeight = (uint)std::stoul(val);
                else if (key == "vsync") vsync = std::stoi(val) != 0;
                else if (key == "fullscreen") fullscreen = std::stoi(val) != 0;
                else if (key == "frontToBackSort") frontToBackSort = std::stoi(val) != 0;
                else if (key == "sortMaxDistance") sortMaxDistance = std::stof(val);
                else if (key == "lodDistanceMultiplier") lodDistanceMultiplier = std::stof(val);
                else if (key == "distanceCullingValue") distanceCullingValue = std::stof(val);
                else if (key == "distanceCullingValueRT") distanceCullingValueRT = std::stof(val);
                else if (key == "maxFrameFilteringCount") maxFrameFilteringCount = (uint)std::stoul(val);
                else if (key == "spacialRadius") spacialRadius = std::stof(val);
                else if (key == "spacialSampleCount") spacialSampleCount = (uint)std::stoul(val);
                else if (key == "reservoirRandBias") reservoirRandBias = std::stof(val);
                else if (key == "reservoirSpacialRandBias") reservoirSpacialRandBias = std::stof(val);
                else if (key == "SHARCSceneScale") SHARCSceneScale = std::stof(val);
                else if (key == "SHARCRadianceScale") SHARCRadianceScale = std::stof(val);
                else if (key == "SHARCRoughnessThreshold") SHARCRoughnessThreshold = std::stof(val);
                else if (key == "SHARCSamplesPerPixel") SHARCSamplesPerPixel = (uint)std::stoul(val);
                else if (key == "SHARCAccumulationFrameNum") SHARCAccumulationFrameNum = (uint)std::stoul(val);
                else if (key == "upscalingTechnique") upscalingTechnique = (uint)std::stoul(val);
                else if (key == "dlssQuality") dlssQuality = std::stoi(val);
                else if (key == "dlssPreset") dlssPreset = std::stoi(val);
                else if (key == "dlssdPreset") dlssdPreset = std::stoi(val);
                else if (key == "camera")
                {
                    std::istringstream ss(val);
                    ss >> cameraPos[0] >> cameraPos[1] >> cameraPos[2]
                       >> cameraRot[0] >> cameraRot[1] >> cameraRot[2] >> cameraRot[3];
                    hasCamera = true;
                }
            }
            else if (section == "uilayout")
            {
                size_t sp = line.rfind(' ');
                if (sp == std::string::npos) continue;
                windows.push_back({ String(line.substr(0, sp)), std::stoi(line.substr(sp + 1)) != 0 });
            }
        }
        return true;
    }
};
Project* Project::instance = nullptr;
