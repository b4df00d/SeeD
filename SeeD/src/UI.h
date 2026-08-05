#pragma once

// Component reflection metadata (PropertyTypes / MemberInfo / ComponentInfo /
// knownComponents) and the metadata generator (ParseCpp) now live in
// ComponentMetaTypes.h, included from World.h. Only the call sites remain in this file.

#include "UIHelpers.h"

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

                if (ImGui::MenuItem(World::instance->playing ? "Stop" : "Play") || IOs::instance->keys.pressed[VK_F2])
                {
                    World::instance->playing = !World::instance->playing;
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
