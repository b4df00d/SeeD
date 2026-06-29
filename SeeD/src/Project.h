#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

class Project
{
public:
    static Project* instance;

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
        std::ofstream f(filePath);
        if (!f.is_open()) { IOs::Log("Fail to write project {}", filePath.c_str()); return; }
        f << "SEEDPROJ 1\n";
        f << "[settings]\n";
        f << "sourceDir " << sourceDir << "\n";
        f << "cacheDir " << cacheDir << "\n";
        f << "startupScene " << startupScene << "\n";
        f << "windowWidth " << windowWidth << "\n";
        f << "windowHeight " << windowHeight << "\n";
        f << "vsync " << (vsync ? 1 : 0) << "\n";
        f << "fullscreen " << (fullscreen ? 1 : 0) << "\n";
        f << "frontToBackSort " << (frontToBackSort ? 1 : 0) << "\n";
        f << "sortMaxDistance " << sortMaxDistance << "\n";
        f << "camera " << cameraPos[0] << " " << cameraPos[1] << " " << cameraPos[2]
          << " " << cameraRot[0] << " " << cameraRot[1] << " " << cameraRot[2] << " " << cameraRot[3] << "\n";
        f << "[uilayout]\n";
        for (auto& w : windows) f << w.name << " " << (w.open ? 1 : 0) << "\n";
        IOs::Log("Project saved to {}", filePath.c_str());
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
