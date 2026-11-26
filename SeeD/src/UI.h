#pragma once

// Component reflection metadata (PropertyTypes / MemberInfo / ComponentInfo /
// knownComponents) and the metadata generator (GenerateComponentMetaData) now live in
// ComponentMetaTypes.h, included from World.h. Only the call sites remain in this file.

struct EditorState
{
    bool show = true;
    World::Entity selectedObject = entityInvalid;
    bool dirtyHierarchy = true;

    String currentWorldPath;

    float4x4 cameraView;
    float4x4 cameraProj;
};
EditorState editorState ;

class EditorWindow
{
public:
    EditorWindow(String _name)
    {
        name = _name;
        guiWindows.push_back(this);
    };
    bool isOpen = false;
    String name;

    void Open()
    {
        isOpen = true;
    }
    virtual void Close()
    {
        isOpen = false;
    }
    virtual void Update() = 0;
    virtual bool KeyEvent() { return false; } // return true if the event is consumed, false if it should be passed to the next window

    // static load save status

    static std::vector<EditorWindow*> guiWindows;
    static void UpdateWindows()
    {
        if (IOs::instance->keys.pressed[VK_TAB])
            editorState.show = !editorState.show;

        if (editorState.show)
        {
            for (uint i = 0; i < guiWindows.size(); i++)
            {
                if (guiWindows[i]->isOpen)
                    guiWindows[i]->Update();
            }
        }
        else
        {
            if (ImGui::BeginMenuBar())
            {
                ImGuiStyle& style = ImGui::GetStyle();
                float width = 0.0f;
                width += ImGui::CalcTextSize("Stop").x;
                width += style.ItemSpacing.x;
                width += ImGui::CalcTextSize("average %.3f ms/frame (%.1f FPS)").x;
                AlignForWidth(width);

                if (ImGui::MenuItem(engineState.playing ? "Stop" : "Play") || IOs::instance->keys.pressed[VK_F2])
                {
                    engineState.playing = !engineState.playing;
                }
                ImGui::Separator();

                //duplicate of profiler framerate info
                ImGui::Text("average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

                ImGui::Separator();

                ImGui::EndMainMenuBar();
            }
        }
    }
};
std::vector<EditorWindow*> EditorWindow::guiWindows;

// The look of the in-game HUD, deliberately unlike the editor's (Moonlight, Renderer.h): translucent
// floating panels, no frame, big bold text. Fonts are loaded in UI::On (Renderer.h); null when the
// font file is missing, and the default font is scaled up instead.
namespace GameStyle
{
    static ImFont* fontLarge = nullptr;
    static ImFont* fontSmall = nullptr;

    static const ImVec4 text = ImVec4(0.96f, 0.96f, 0.96f, 1.0f);
    static const ImVec4 dim = ImVec4(0.62f, 0.64f, 0.68f, 1.0f);
    static const ImVec4 gold = ImVec4(1.0f, 0.78f, 0.22f, 1.0f);
    static const ImVec4 green = ImVec4(0.35f, 0.9f, 0.45f, 1.0f);
    static const ImVec4 red = ImVec4(1.0f, 0.35f, 0.3f, 1.0f);

    static void Push()
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.03f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_Text, text);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.78f, 0.22f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.78f, 0.22f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1.0f, 1.0f, 1.0f, 0.04f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.0f, 2.0f));
    }

    static void Pop()
    {
        ImGui::PopStyleVar(7);
        ImGui::PopStyleColor(8);
    }

    // Call right after Begin: font for the rest of the window. Returns whether a font was pushed.
    static bool PushFont(ImFont* font, float fallbackScale)
    {
        if (font != nullptr)
        {
            ImGui::PushFont(font);
            return true;
        }
        ImGui::SetWindowFontScale(fallbackScale);
        return false;
    }
    static void PopFont(bool pushed)
    {
        if (pushed)
            ImGui::PopFont();
        else
            ImGui::SetWindowFontScale(1.0f);
    }
}

// In-game UI (HUD), as opposed to EditorWindow tools: own list, own update loop, own style. Not in
// the View menu, not in the project's UI layout, not hidden by TAB, only drawn while playing.
// Engine schedules GameWindow::UpdateWindows BEFORE EditorWindow::UpdateWindows -- game frame and
// HUD first, editor on top -- and the windows never take the front, so the editor stays above.
// Owned by whatever drives them (a system registers its window in On and removes it in Off).
class GameWindow
{
public:
    virtual ~GameWindow() = default;
    virtual void Update() = 0;

    // ImGui flags every game window should carry: no docking into the editor dockspace, no
    // imgui.ini entry, never brought in front of the editor windows.
    static constexpr ImGuiWindowFlags baseFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse;

    static std::vector<GameWindow*> gameWindows;

    static void Register(GameWindow* window)
    {
        if (std::find(gameWindows.begin(), gameWindows.end(), window) == gameWindows.end())
            gameWindows.push_back(window);
    }
    static void Unregister(GameWindow* window)
    {
        auto item = std::find(gameWindows.begin(), gameWindows.end(), window);
        if (item != gameWindows.end())
            gameWindows.erase(item);
    }

    static void UpdateWindows()
    {
        ZoneScoped;
        if (!engineState.playing)
            return;
        GameStyle::Push();
        for (GameWindow* window : gameWindows)
            window->Update();
        GameStyle::Pop();
    }
};
std::vector<GameWindow*> GameWindow::gameWindows;
