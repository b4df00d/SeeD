#pragma once




class EditorWindow
{
public:
    EditorWindow(String _name)
    {
        name = _name;
        guiWindows.push_back(this);
    };
    static std::vector<EditorWindow*> guiWindows;
    bool isOpen = false;
    String name;
    static void UpdateWindows()
    {
        for (uint i = 0; i < guiWindows.size(); i++)
        {
            guiWindows[i]->Update();
        }
    }
    static void DisplayMenu()
    {
        for (uint i = 0; i < guiWindows.size(); i++)
        {
            if (ImGui::MenuItem(guiWindows[i]->name.c_str()))
            {
                guiWindows[i]->Open();
            }
        }
    }

    void Open()
    {
        isOpen = true;
    }
    virtual void Update() = 0;
};
std::vector<EditorWindow*> EditorWindow::guiWindows;

class ProfilerWindow : public EditorWindow
{
public:
    ProfilerWindow() : EditorWindow("Profiler") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("Profiler", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }
        ImGui::Text("average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        ImGui::Text("instances %u | meshlets %u", Profiler::instance->instancesCount, Profiler::instance->meshletsCount);

        ImGui::Separator();

        for (int i = 0; i < Profiler::instance->profiles.size(); i++)
        {
            Profiler::ProfileData& profile = Profiler::instance->profiles[i];
            if (profile.name == nullptr)
                continue;

            double maxTime = 0.0;
            double avgTime = 0.0;
            UINT64 avgTimeSamples = 0;
            for (UINT i = 0; i < Profiler::ProfileData::FilterSize; ++i)
            {
                if (profile.TimeSamples[i] <= 0.0)
                    continue;
                maxTime = profile.TimeSamples[i] > maxTime ? profile.TimeSamples[i] : maxTime;
                avgTime += profile.TimeSamples[i];
                ++avgTimeSamples;
            }

            if (avgTimeSamples > 0)
                avgTime /= double(avgTimeSamples);

            ImGui::Text("%s: %.2fms (%.2fms max)", profile.name, avgTime, maxTime);
        }

        ImGui::End();
    }
};
ProfilerWindow profilerWindow;

class AssetLibraryWindow : public EditorWindow
{
public:
    AssetLibraryWindow() : EditorWindow("AssetLibrary") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("AssetLibrary", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }
        
        if (ImGui::Button("Clear"))
        {
            AssetLibrary::instance->map.clear();
        }
        ImGui::Separator();

        for (auto& item : AssetLibrary::instance->map)
        {
            ImGui::RadioButton("##radio", item.second.indexInVector != ~0);
            ImGui::SameLine();
            ImGui::Text("%ul %s", item.first, item.second.path.c_str());
        }

        ImGui::End();
    }
};
AssetLibraryWindow assetLibraryWindowWindow;

class GPUResourcesWindow : public EditorWindow
{
    bool openDemo;
public:
    GPUResourcesWindow() : EditorWindow("GPUResources") {}
    void Update() override final
    {
        ZoneScoped;

        ImGui::ShowDemoWindow(&openDemo);

        if (!ImGui::Begin("GPUResources", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        const std::lock_guard<std::mutex> lock(Resource::lock);

        const char* names[] = { "Resources", "Content" };
        static Resource* selectedResource = nullptr;
        static int mip = 0;
        bool selected;

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::BeginGroup();
        const ImGuiWindowFlags child_flags = ImGuiWindowFlags_MenuBar;
        ImGuiID child_id = ImGui::GetID((void*)(intptr_t)0);
        const bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(200, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders, child_flags);
        for (int i = 0; i < Resource::allResources.size(); i++)
        {

            ImGui::PushID(i);
            selected = selectedResource == Resource::allResources[i];
            if (selected)
            {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), Resource::allResourcesNames[i].c_str());
            }
            else
            {
                ImGui::Selectable(Resource::allResourcesNames[i].c_str(), &selected);
                if (selected)
                    selectedResource = Resource::allResources[i];
            }
            ImGui::SameLine();
            ImGui::Text("%u", Resource::allResources[i]->GetResource()->GetDesc().Width);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        child_id = ImGui::GetID((void*)(intptr_t)1);
        ImGui::BeginChild(child_id, ImGui::GetContentRegionAvail(), ImGuiChildFlags_Borders, child_flags);
        if (selectedResource)
        {
            ImGuiIO& io = ImGui::GetIO();
            auto desc = selectedResource->GetResource()->GetDesc();
            float textureW = desc.Width;
            float textureH = desc.Height;
            float ratio = textureW / textureH;
            ImVec2 img_sz = ImGui::GetContentRegionAvail();
            img_sz.y = img_sz.x / ratio;
            {
                ImGui::Text("%.0fx%.0f", textureW, textureH);
                ImGui::SameLine();
                ImGui::SliderInt("mip", &mip, 0, desc.MipLevels-1);

                seedAssert(mip < desc.MipLevels);

                // Create texture view
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
                ZeroMemory(&srvDesc, sizeof(srvDesc));
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = -1;// desc.MipLevels;
                srvDesc.Texture2D.MostDetailedMip = mip;
                srvDesc.Texture2D.PlaneSlice = 0;
                srvDesc.Texture2D.ResourceMinLODClamp = 0;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                GPU::instance->device->CreateShaderResourceView(selectedResource->GetResource(), &srvDesc, UI::instance->imgCPUHandle);
                ImTextureID my_tex_id = UI::instance->imgGPUHandle.ptr;

                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 uv_min = ImVec2(0.0f, 0.0f);                 // Top-left
                ImVec2 uv_max = ImVec2(1.0f, 1.0f);                 // Lower-right
                ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);   // No tint
                ImVec4 border_col = ImGui::GetStyleColorVec4(ImGuiCol_Border);
                child_id = ImGui::GetID((void*)(intptr_t)2);
                ImGui::Image(my_tex_id, img_sz, uv_min, uv_max, tint_col, border_col);
                if (ImGui::BeginItemTooltip())
                {
                    ImVec2 imgPos = ImVec2(pos.x, pos.y);

                    float region_sz = 16.0f;
                    float region_x = io.MousePos.x - pos.x - region_sz * 0.5f;
                    float region_y = io.MousePos.y - pos.y - region_sz * 0.5f;
                    float zoom = 16.0f;
                    if (region_x < 0.0f) { region_x = 0.0f; }
                    else if (region_x > img_sz.x - region_sz) { region_x = img_sz.x - region_sz; }
                    if (region_y < 0.0f) { region_y = 0.0f; }
                    else if (region_y > img_sz.y - region_sz) { region_y = img_sz.y - region_sz; }
                    ImGui::Text("Min: (%.2f, %.2f)", region_x, region_y);
                    ImGui::Text("Max: (%.2f, %.2f)", region_x + region_sz, region_y + region_sz);
                    ImVec2 uv0 = ImVec2((region_x) / img_sz.x, (region_y) / img_sz.y);
                    ImVec2 uv1 = ImVec2((region_x + region_sz) / img_sz.x, (region_y + region_sz) / img_sz.y);
                    ImGui::Image(my_tex_id, ImVec2(region_sz * zoom, region_sz * zoom), uv0, uv1, tint_col, border_col);
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::End();
    }
};
GPUResourcesWindow gpuResourcesWindow;

#include "Shaders/structs.hlsl"
class OptionWindow : public EditorWindow
{
public:
    OptionWindow() : EditorWindow("Option") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("Option", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("stopFrustumUpdate", &options.stopFrustumUpdate);
        ImGui::Checkbox("stopBufferUpload", &options.stopBufferUpload);
        ImGui::Checkbox("stepMotion", &options.stepMotion);

        ImGui::End();
    }
};
OptionWindow optionWindow;

/*
class AboutWindow : public EditorWindow
{
public:
    AboutWindow() : EditorWindow("About") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("About Dear ImGui", &isOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }
        //IMGUI_DEMO_MARKER("Tools/About Dear ImGui");
        ImGui::Text("Dear ImGui %s (%d)", IMGUI_VERSION, IMGUI_VERSION_NUM);

        ImGui::TextLinkOpenURL("Homepage", "https://github.com/ocornut/imgui");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("FAQ", "https://github.com/ocornut/imgui/blob/master/docs/FAQ.md");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("Wiki", "https://github.com/ocornut/imgui/wiki");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("Releases", "https://github.com/ocornut/imgui/releases");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("Funding", "https://github.com/ocornut/imgui/wiki/Funding");

        ImGui::Separator();
        ImGui::Text("By Omar Cornut and all Dear ImGui contributors.");
        ImGui::Text("Dear ImGui is licensed under the MIT License, see LICENSE for more information.");
        ImGui::Text("If your company uses this, please consider funding the project.");

        static bool show_config_info = false;
        ImGui::Checkbox("Config/Build Information", &show_config_info);
        if (show_config_info)
        {
            ImGuiIO& io = ImGui::GetIO();
            ImGuiStyle& style = ImGui::GetStyle();

            bool copy_to_clipboard = ImGui::Button("Copy to clipboard");
            ImVec2 child_size = ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 18);
            ImGui::BeginChild(ImGui::GetID("cfg_infos"), child_size, ImGuiChildFlags_FrameStyle);
            if (copy_to_clipboard)
            {
                ImGui::LogToClipboard();
                ImGui::LogText("```\n"); // Back quotes will make text appears without formatting when pasting on GitHub
            }

            ImGui::Text("Dear ImGui %s (%d)", IMGUI_VERSION, IMGUI_VERSION_NUM);
            ImGui::Separator();
            ImGui::Text("sizeof(size_t): %d, sizeof(ImDrawIdx): %d, sizeof(ImDrawVert): %d", (int)sizeof(size_t), (int)sizeof(ImDrawIdx), (int)sizeof(ImDrawVert));
            ImGui::Text("define: __cplusplus=%d", (int)__cplusplus);
#ifdef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_OBSOLETE_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_WIN32_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_WIN32_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_FILE_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_FILE_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_DEFAULT_ALLOCATORS
            ImGui::Text("define: IMGUI_DISABLE_DEFAULT_ALLOCATORS");
#endif
#ifdef IMGUI_USE_BGRA_PACKED_COLOR
            ImGui::Text("define: IMGUI_USE_BGRA_PACKED_COLOR");
#endif
#ifdef _WIN32
            ImGui::Text("define: _WIN32");
#endif
#ifdef _WIN64
            ImGui::Text("define: _WIN64");
#endif
#ifdef __linux__
            ImGui::Text("define: __linux__");
#endif
#ifdef __APPLE__
            ImGui::Text("define: __APPLE__");
#endif
#ifdef _MSC_VER
            ImGui::Text("define: _MSC_VER=%d", _MSC_VER);
#endif
#ifdef _MSVC_LANG
            ImGui::Text("define: _MSVC_LANG=%d", (int)_MSVC_LANG);
#endif
#ifdef __MINGW32__
            ImGui::Text("define: __MINGW32__");
#endif
#ifdef __MINGW64__
            ImGui::Text("define: __MINGW64__");
#endif
#ifdef __GNUC__
            ImGui::Text("define: __GNUC__=%d", (int)__GNUC__);
#endif
#ifdef __clang_version__
            ImGui::Text("define: __clang_version__=%s", __clang_version__);
#endif
#ifdef __EMSCRIPTEN__
            ImGui::Text("define: __EMSCRIPTEN__");
            ImGui::Text("Emscripten: %d.%d.%d", __EMSCRIPTEN_major__, __EMSCRIPTEN_minor__, __EMSCRIPTEN_tiny__);
#endif
#ifdef IMGUI_HAS_VIEWPORT
            ImGui::Text("define: IMGUI_HAS_VIEWPORT");
#endif
#ifdef IMGUI_HAS_DOCK
            ImGui::Text("define: IMGUI_HAS_DOCK");
#endif
            ImGui::Separator();
            ImGui::Text("io.BackendPlatformName: %s", io.BackendPlatformName ? io.BackendPlatformName : "NULL");
            ImGui::Text("io.BackendRendererName: %s", io.BackendRendererName ? io.BackendRendererName : "NULL");
            ImGui::Text("io.ConfigFlags: 0x%08X", io.ConfigFlags);
            if (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard)        ImGui::Text(" NavEnableKeyboard");
            if (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad)         ImGui::Text(" NavEnableGamepad");
            if (io.ConfigFlags & ImGuiConfigFlags_NoMouse)                  ImGui::Text(" NoMouse");
            if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)      ImGui::Text(" NoMouseCursorChange");
            if (io.ConfigFlags & ImGuiConfigFlags_NoKeyboard)               ImGui::Text(" NoKeyboard");
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)            ImGui::Text(" DockingEnable");
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)          ImGui::Text(" ViewportsEnable");
            if (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)  ImGui::Text(" DpiEnableScaleViewports");
            if (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleFonts)      ImGui::Text(" DpiEnableScaleFonts");
            if (io.MouseDrawCursor)                                         ImGui::Text("io.MouseDrawCursor");
            if (io.ConfigViewportsNoAutoMerge)                              ImGui::Text("io.ConfigViewportsNoAutoMerge");
            if (io.ConfigViewportsNoTaskBarIcon)                            ImGui::Text("io.ConfigViewportsNoTaskBarIcon");
            if (io.ConfigViewportsNoDecoration)                             ImGui::Text("io.ConfigViewportsNoDecoration");
            if (io.ConfigViewportsNoDefaultParent)                          ImGui::Text("io.ConfigViewportsNoDefaultParent");
            if (io.ConfigDockingNoSplit)                                    ImGui::Text("io.ConfigDockingNoSplit");
            if (io.ConfigDockingWithShift)                                  ImGui::Text("io.ConfigDockingWithShift");
            if (io.ConfigDockingAlwaysTabBar)                               ImGui::Text("io.ConfigDockingAlwaysTabBar");
            if (io.ConfigDockingTransparentPayload)                         ImGui::Text("io.ConfigDockingTransparentPayload");
            if (io.ConfigMacOSXBehaviors)                                   ImGui::Text("io.ConfigMacOSXBehaviors");
            if (io.ConfigNavMoveSetMousePos)                                ImGui::Text("io.ConfigNavMoveSetMousePos");
            if (io.ConfigNavCaptureKeyboard)                                ImGui::Text("io.ConfigNavCaptureKeyboard");
            if (io.ConfigInputTextCursorBlink)                              ImGui::Text("io.ConfigInputTextCursorBlink");
            if (io.ConfigWindowsResizeFromEdges)                            ImGui::Text("io.ConfigWindowsResizeFromEdges");
            if (io.ConfigWindowsMoveFromTitleBarOnly)                       ImGui::Text("io.ConfigWindowsMoveFromTitleBarOnly");
            if (io.ConfigMemoryCompactTimer >= 0.0f)                        ImGui::Text("io.ConfigMemoryCompactTimer = %.1f", io.ConfigMemoryCompactTimer);
            ImGui::Text("io.BackendFlags: 0x%08X", io.BackendFlags);
            if (io.BackendFlags & ImGuiBackendFlags_HasGamepad)             ImGui::Text(" HasGamepad");
            if (io.BackendFlags & ImGuiBackendFlags_HasMouseCursors)        ImGui::Text(" HasMouseCursors");
            if (io.BackendFlags & ImGuiBackendFlags_HasSetMousePos)         ImGui::Text(" HasSetMousePos");
            if (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports)   ImGui::Text(" PlatformHasViewports");
            if (io.BackendFlags & ImGuiBackendFlags_HasMouseHoveredViewport)ImGui::Text(" HasMouseHoveredViewport");
            if (io.BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset)   ImGui::Text(" RendererHasVtxOffset");
            if (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports)   ImGui::Text(" RendererHasViewports");
            ImGui::Separator();
            ImGui::Text("io.Fonts: %d fonts, Flags: 0x%08X, TexSize: %d,%d", io.Fonts->Fonts.Size, io.Fonts->Flags, io.Fonts->TexWidth, io.Fonts->TexHeight);
            ImGui::Text("io.DisplaySize: %.2f,%.2f", io.DisplaySize.x, io.DisplaySize.y);
            ImGui::Text("io.DisplayFramebufferScale: %.2f,%.2f", io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            ImGui::Separator();
            ImGui::Text("style.WindowPadding: %.2f,%.2f", style.WindowPadding.x, style.WindowPadding.y);
            ImGui::Text("style.WindowBorderSize: %.2f", style.WindowBorderSize);
            ImGui::Text("style.FramePadding: %.2f,%.2f", style.FramePadding.x, style.FramePadding.y);
            ImGui::Text("style.FrameRounding: %.2f", style.FrameRounding);
            ImGui::Text("style.FrameBorderSize: %.2f", style.FrameBorderSize);
            ImGui::Text("style.ItemSpacing: %.2f,%.2f", style.ItemSpacing.x, style.ItemSpacing.y);
            ImGui::Text("style.ItemInnerSpacing: %.2f,%.2f", style.ItemInnerSpacing.x, style.ItemInnerSpacing.y);

            if (copy_to_clipboard)
            {
                ImGui::LogText("\n```\n");
                ImGui::LogFinish();
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
};
AboutWindow aboutWindow;
*/
