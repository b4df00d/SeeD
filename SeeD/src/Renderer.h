#pragma once

#include <map>
#include "Shaders/structs.hlsl"

#include "ffx_api/ffx_api.hpp"
#include "ffx_api/dx12/ffx_api_dx12.hpp"
#include "FidelityFX/host/backends/dx12/ffx_dx12.h"
#include "FidelityFX/host/ffx_spd.h"

class UI
{
    ID3D12DescriptorHeap* pd3dSrvDescHeap = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE  hFontSrvCpuDescHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE  hFontSrvGpuDescHandle = {};
    ID3D12GraphicsCommandList* cmdList = nullptr;
    ID3D12CommandAllocator* cmdAlloc = nullptr;

public:

    D3D12_CPU_DESCRIPTOR_HANDLE  imgCPUHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE  imgGPUHandle = {};
    static UI* instance;
    void On(IOs::WindowInformation* window, ID3D12Device9* device, IDXGISwapChain3* swapchain)
    {
        ZoneScoped;
        instance = this;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui_ImplWin32_Init(window->windowHandle);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // IF using Docking Branch
        if (Project::instance) io.IniFilename = Project::instance->imguiIniPath.c_str();


        DXGI_SWAP_CHAIN_DESC sc;
        swapchain->GetDesc(&sc);

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 100;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pd3dSrvDescHeap)) != S_OK)
            return;

        hFontSrvCpuDescHandle = pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
        hFontSrvGpuDescHandle = pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart();

        imgCPUHandle.ptr = static_cast<SIZE_T>(pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(1) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));
        imgGPUHandle.ptr = static_cast<SIZE_T>(pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart().ptr + INT64(1) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        ImGui_ImplDX12_Init(device, sc.BufferCount, sc.BufferDesc.Format, pd3dSrvDescHeap, hFontSrvCpuDescHandle, hFontSrvGpuDescHandle);

        SetupImGuiStyle();
    }

    void Off()
    {
        ZoneScoped;
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    void FrameStart()
    {
        ZoneScoped;
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags host_window_flags = 0;
        host_window_flags |= ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            host_window_flags |= ImGuiWindowFlags_NoBackground;


        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("SeeD", NULL, host_window_flags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspace_id = ImGui::GetID("DockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }

    void FrameRender(ID3D12GraphicsCommandList4* cmdList)
    {
        ZoneScoped;
        ImGui::Render();
        cmdList->SetDescriptorHeaps(1, &pd3dSrvDescHeap);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
    }

    void SetupImGuiStyle()
    {
        // Moonlight style by Madam-Herta from ImThemes
        ImGuiStyle& style = ImGui::GetStyle();

        style.Alpha = 1.0f;
        style.DisabledAlpha = 1.0f;
        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.WindowRounding = 11.5f;
        style.WindowBorderSize = 0.0f;
        style.WindowMinSize = ImVec2(20.0f, 20.0f);
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Right;
        style.ChildRounding = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupRounding = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.FramePadding = ImVec2(20.0f, 3.4f);
        style.FrameRounding = 11.9f;
        style.FrameBorderSize = 0.0f;
        style.ItemSpacing = ImVec2(4.3f, 5.5f);
        style.ItemInnerSpacing = ImVec2(7.1f, 1.8f);
        style.CellPadding = ImVec2(12.1f, 9.2f);
        style.IndentSpacing = 0.0f;
        style.ColumnsMinSpacing = 4.9f;
        style.ScrollbarSize = 11.6f;
        style.ScrollbarRounding = 15.9f;
        style.GrabMinSize = 3.7f;
        style.GrabRounding = 20.0f;
        style.TabRounding = 0.0f;
        style.TabBorderSize = 0.0f;
        style.TabMinWidthForCloseButton = 0.0f;
        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

        style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.27450982f, 0.31764707f, 0.4509804f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09411765f, 0.101960786f, 0.11764706f, 1.0f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.11372549f, 0.1254902f, 0.15294118f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.09803922f, 0.105882354f, 0.12156863f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.972549f, 1.0f, 0.49803922f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.972549f, 1.0f, 0.89803922f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.472549f, 1.0f, 0.89803922f, 1.0f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.972549f, 1.0f, 0.49803922f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.972549f, 1.0f, 0.49803922f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.79607844f, 0.49803922f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18039216f, 0.1882353f, 0.19607843f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.15294118f, 0.15294118f, 0.15294118f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.14117648f, 0.16470589f, 0.20784314f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.105882354f, 0.105882354f, 0.105882354f, 1.0f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.12941177f, 0.14901961f, 0.19215687f, 1.0f);
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.15686275f, 0.18431373f, 0.2509804f, 1.0f);
        style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.15686275f, 0.18431373f, 0.2509804f, 1.0f);
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.14509805f, 0.14509805f, 0.14509805f, 1.0f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.972549f, 1.0f, 0.49803922f, 1.0f);
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1254902f, 0.27450982f, 0.57254905f, 1.0f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.52156866f, 0.6f, 0.7019608f, 1.0f);
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.039215688f, 0.98039216f, 0.98039216f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.88235295f, 0.79607844f, 0.56078434f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.95686275f, 0.95686275f, 0.95686275f, 1.0f);
        style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
        style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
        style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
        style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.09803922f, 0.105882354f, 0.12156863f, 1.0f);
        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.9372549f, 0.9372549f, 0.9372549f, 1.0f);
        style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.49803922f, 0.5137255f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.26666668f, 0.2901961f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.49803922f, 0.5137255f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.19607843f, 0.1764706f, 0.54509807f, 0.5019608f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.19607843f, 0.1764706f, 0.54509807f, 0.5019608f);
    }
};
UI* UI::instance;

struct ShaderBucketInfo
{
    assetID shaderAssetId;
    uint baseOffset;
    uint meshletCount;
};

// Debug-UI snapshot of one bucket's state; see ShaderBucketRegistry::GetDebugStats.
struct ShaderBucketDebugEntry
{
    assetID shaderAssetId;
    uint bucketIndex;
    uint meshletCount;
    uint instanceCount;
};

// Persistent (NOT rebuilt every frame) registry of the distinct Shader assets referenced by scene
// materials, backing the generic multi-shader GBuffer draw-indirect: one ExecuteIndirect per shader
// found (GBuffers::Render), routed GPU-side by a per-material shaderIndex (structs.hlsl
// HLSL::Material.shaderIndex / culling.hlsl) instead of per-shader bool flags.
//
// Bucket *indices* are stable for the life of the view: assigned once per distinct Shader assetID
// the first time a material references it (GetOrRegister), and never reassigned/reused -- because a
// material's shaderIndex, once written to the GPU materials buffer, is only re-touched when that
// material is dirty again (UpdateMaterials), so shifting index assignment across frames would leave
// untouched materials silently pointing at the wrong bucket.
//
// Meshlet counts are maintained INCREMENTALLY (added on an instance's first load, subtracted on
// release -- see AddInstanceContribution/ReleaseInstanceContribution), NOT recomputed from a
// per-frame scan: UpdateInstances skips loaded-and-not-dirty instances for performance, so a naive
// "sum what ran this frame" total would silently undercount once instances settle into the
// non-dirty steady state.
class ShaderBucketRegistry
{
    std::mutex lock;
    std::unordered_map<assetID, uint> bucketIndex;         // assetID -> stable bucket index
    std::vector<assetID> bucketShaderAssetId;               // index -> assetID
    // std::deque, not std::vector: std::atomic is non-movable/non-copyable so a vector could never
    // grow past its first allocation, and deque additionally guarantees push/emplace_back at the
    // end never invalidates references to existing elements -- required here since callers look up
    // a bucket's atomic once (under lock) and may keep using that reference afterward.
    std::deque<std::atomic<uint32_t>> bucketMeshletCounts;  // index -> current live meshlet count
    // instance -> (bucketIndex, meshletCount) it added on first load, so release can subtract
    // exactly that back out.
    std::unordered_map<World::Entity, std::pair<uint, uint>> instanceContribution;

    // Caller must hold lock. Shared by GetOrRegister and AddInstanceContribution so lookup-or-register
    // and any follow-up counter mutation happen under a single critical section.
    uint GetOrRegister_NoLock(assetID shaderId)
    {
        auto it = bucketIndex.find(shaderId);
        if (it != bucketIndex.end())
            return it->second;
        uint index = (uint)bucketShaderAssetId.size();
        bucketIndex[shaderId] = index;
        bucketShaderAssetId.push_back(shaderId);
        bucketMeshletCounts.emplace_back(0u);
        return index;
    }

public:
    // Look up (or, if never seen before, register) the stable bucket index for a Shader asset.
    // Thread-safe; called from UpdateMaterials for every material processed this frame.
    uint GetOrRegister(assetID shaderId)
    {
        std::lock_guard<std::mutex> lk(lock);
        return GetOrRegister_NoLock(shaderId);
    }

    // Registers (first-load only) this instance's meshlet-count contribution to its shader's
    // bucket, and records exactly what was added so ReleaseInstanceContribution can undo it
    // precisely. Everything happens under one lock so the bucket lookup/registration and the
    // counter increment are atomic with respect to each other and to concurrent releases.
    void AddInstanceContribution(World::Entity entity, assetID shaderId, uint meshletCount)
    {
        std::lock_guard<std::mutex> lk(lock);
        uint bucket = GetOrRegister_NoLock(shaderId);
        bucketMeshletCounts[bucket].fetch_add(meshletCount, std::memory_order_relaxed);
        instanceContribution[entity] = { bucket, meshletCount };
    }

    // Undo exactly the meshlet-count contribution an instance added on its first load, so a
    // released instance's shader bucket shrinks back down instead of leaking a stale over-large
    // count into next frame's buffer sizing.
    void ReleaseInstanceContribution(World::Entity entity)
    {
        std::lock_guard<std::mutex> lk(lock);
        auto it = instanceContribution.find(entity);
        if (it == instanceContribution.end())
            return;
        uint bucket = it->second.first;
        uint meshletCount = it->second.second;
        if (bucket < bucketMeshletCounts.size())
            bucketMeshletCounts[bucket].fetch_sub(meshletCount, std::memory_order_relaxed);
        instanceContribution.erase(it);
    }

    // Exclusive prefix-sum the persistent, incrementally-maintained per-bucket meshlet counts (see
    // AddInstanceContribution/ReleaseInstanceContribution -- NOT a per-frame instance scan, which
    // would silently undercount once instances settle into the loaded-and-not-dirty steady state)
    // into per-bucket base offsets for the single shared meshletsCulledArgs buffer, and snapshot
    // (shaderAssetId, baseOffset, meshletCount) per bucket for GBuffers::Render to iterate. O(bucket
    // count), not O(instance count). Returns the total meshlet count across every bucket.
    uint BuildFrameSnapshot(StructuredUploadBuffer<uint>& offsets, std::vector<ShaderBucketInfo>& buckets)
    {
        std::lock_guard<std::mutex> lk(lock);
        uint bucketCount = (uint)bucketMeshletCounts.size();
        offsets.Clear();
        buckets.clear();
        uint totalMeshlets = 0;
        for (uint b = 0; b < bucketCount; b++)
        {
            uint count = bucketMeshletCounts[b].load(std::memory_order_relaxed);
            offsets.Add(totalMeshlets); // exclusive prefix sum
            ShaderBucketInfo info;
            info.shaderAssetId = bucketShaderAssetId[b];
            info.baseOffset = totalMeshlets;
            info.meshletCount = count;
            buckets.push_back(info);
            totalMeshlets += count;
        }
        offsets.Upload();
        return totalMeshlets;
    }

    // Debug-UI-only snapshot of every bucket's live state, including per-bucket instance count
    // (instanceContribution is only ever as large as the currently-loaded instance count, so
    // this O(bucket count + instance count) walk is fine for an opt-in, not-every-frame window --
    // unlike BuildFrameSnapshot, which runs unconditionally every frame and must stay O(bucket count)).
    std::vector<ShaderBucketDebugEntry> GetDebugStats()
    {
        std::lock_guard<std::mutex> lk(lock);
        uint bucketCount = (uint)bucketMeshletCounts.size();
        std::vector<ShaderBucketDebugEntry> result(bucketCount);
        for (uint b = 0; b < bucketCount; b++)
        {
            result[b].shaderAssetId = bucketShaderAssetId[b];
            result[b].bucketIndex = b;
            result[b].meshletCount = bucketMeshletCounts[b].load(std::memory_order_relaxed);
            result[b].instanceCount = 0;
        }
        for (auto& kv : instanceContribution)
        {
            uint bucket = kv.second.first;
            if (bucket < result.size())
                result[bucket].instanceCount++;
        }
        return result;
    }
};

// life time : frame
struct ViewWorld
{
    HLSL::CommonResourcesIndices commonResourcesIndices;
    PerFrame<StructuredUploadBuffer<HLSL::Camera>> cameras;
    PerFrame<StructuredUploadBuffer<HLSL::Light>> lights;
    DirtyTrackingStructuredBuffer<World::Entity, HLSL::Material> materials;
    DirtyTrackingStructuredBuffer<World::Entity, HLSL::Instance> instances;

    ShaderBucketRegistry shaderBucketRegistry;
    PerFrame<StructuredUploadBuffer<uint>> shaderBucketOffsets;
    std::vector<ShaderBucketInfo> shaderBuckets; // this frame's snapshot, for GBuffers::Render to iterate
    std::vector<HLSL::Instance> instancesReadBackDebug;

    std::atomic<uint> meshletsCount;

    void On()
    {
        ZoneScoped;

        Components::RemoveCallback[Components::Instance::bucketIndex] = [this](EntityBase entity)
        {
            instances.Remove(World::Entity(entity));
            shaderBucketRegistry.ReleaseInstanceContribution(World::Entity(entity));
        };
    }

    void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            cameras.Get(i).Release();
            lights.Get(i).Release();
            shaderBucketOffsets.Get(i).Release();
        }
        materials.Release();
        instances.Release();
    }
};

// life time : view (only updated on GPU)
struct ViewContext
{
    HLSL::ViewContext viewContext; // to bind to rootSig

    StructuredBuffer<HLSL::Camera> camera;
    StructuredBuffer<HLSL::Light> lights;
    StructuredBuffer<HLSL::InstanceCullingDispatch> instancesCulledArgs;
    StructuredBuffer<HLSL::GroupedCullingDispatch> meshletsToCull;
    StructuredBuffer<HLSL::MeshletDrawCall> meshletsCulledArgs; // single shared draw-args buffer: every shader bucket owns a region at its own prefix-sum base offset (see ViewWorld::shaderBuckets)
    StructuredBuffer<HLSL::MeshletDrawCall> meshletsCulledArgsSorted; // front-to-back ordered copy of bucket 0 only (the sort-eligible default opaque shader)
    StructuredBuffer<uint> sortHistogram; // depth-bucket counts / offsets for the draw sort
    StructuredBuffer<uint> meshletBuckets; // per-culled-meshlet depth bucket, filled during culling
    StructuredBuffer<uint> instancesCounter;
    StructuredBuffer<uint> meshletsCounter;

    float2 jitter[16] = {
        float2(0.500000, 0.333333),
        float2(0.250000, 0.666667),
        float2(0.750000, 0.111111),
        float2(0.125000, 0.444444),
        float2(0.625000, 0.777778),
        float2(0.375000, 0.222222),
        float2(0.875000, 0.555556),
        float2(0.062500, 0.888889),
        float2(0.562500, 0.037037),
        float2(0.312500, 0.370370),
        float2(0.812500, 0.703704),
        float2(0.187500, 0.148148),
        float2(0.687500, 0.481481),
        float2(0.437500, 0.814815),
        float2(0.937500, 0.259259),
        float2(0.031250, 0.592593)
    };
    uint jitterIndex;

    void On()
    {
        instancesCulledArgs.CreateBuffer(10, D3D12_RESOURCE_STATE_COMMON);
        meshletsToCull.CreateBuffer(100, D3D12_RESOURCE_STATE_COMMON);
        instancesCounter.CreateBuffer(2, D3D12_RESOURCE_STATE_COMMON);
        meshletsCulledArgs.CreateBuffer(100, D3D12_RESOURCE_STATE_COMMON);
        meshletsCulledArgsSorted.CreateBuffer(100, D3D12_RESOURCE_STATE_COMMON);
        sortHistogram.CreateBuffer(SORT_BUCKETS, D3D12_RESOURCE_STATE_COMMON);
        meshletBuckets.CreateBuffer(100, D3D12_RESOURCE_STATE_COMMON);
        meshletsCounter.CreateBuffer(1, D3D12_RESOURCE_STATE_COMMON);
        jitterIndex = 0;
    }

    void Off()
    {
        camera.Release();
        lights.Release();
        instancesCulledArgs.Release();
        meshletsToCull.Release();
        meshletsCulledArgs.Release();
        meshletsCulledArgsSorted.Release();
        sortHistogram.Release();
        meshletBuckets.Release();
        instancesCounter.Release();
        meshletsCounter.Release();
    }
};

struct EditorContext
{
    HLSL::EditorContext editorContext;
    StructuredBuffer<HLSL::IndirectCommand> indirectDebugBuffer;
    StructuredBuffer<HLSL::DebugVertex> indirectDebugVertices;
    StructuredBuffer<uint> indirectDebugVerticesCount; // draw count, vertex index count

    StructuredBuffer<HLSL::SelectionResult> selectionResult;

    void On()
    {
        indirectDebugBuffer.CreateBuffer(100, D3D12_RESOURCE_STATE_COMMON);
        indirectDebugVertices.CreateBuffer(1000000, D3D12_RESOURCE_STATE_COMMON);
        indirectDebugVerticesCount.CreateBuffer(100, D3D12_RESOURCE_STATE_COMMON);

        selectionResult.CreateBuffer(100, D3D12_RESOURCE_STATE_COMMON);
    }

    void Off()
    {
        indirectDebugBuffer.Release();
        indirectDebugVertices.Release();
        indirectDebugVerticesCount.Release();

        selectionResult.Release();
    }
};

struct RayTracingContext
{
    HLSL::RTParameters rtParameters;
    StructuredBuffer<D3D12_RAYTRACING_INSTANCE_DESC> instancesRayTracing;
    StructuredBuffer<uint> instancesRayTracingCount;
    Resource TLAS;
    PerFrame<Resource> giReservoir;

    Resource SHARCHash;
    Resource SHARCAccumulation;
    Resource SHARCResolved;

    void On(uint2 resolution)
    {
        instancesRayTracingCount.CreateBuffer(1, D3D12_RESOURCE_STATE_COMMON);
        TLAS.CreateAccelerationStructure(64 * 1024 * 1024, "TLAS");

        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            giReservoir.Get(i).CreateBuffer<HLSL::GIReservoirCompressed>(resolution.x * resolution.y, "GIReservoir");
        }
        uint SHARCEntryCount = (uint)pow(2, 22);
        SHARCHash.CreateBuffer(SHARCEntryCount * 8, 8, false, "SHARCHash");
        SHARCAccumulation.CreateBuffer(SHARCEntryCount * 16, 16, false, "SHARCAccumulation");
        SHARCResolved.CreateBuffer(SHARCEntryCount * 16, 16, false, "SHARCResolved");

        rtParameters.maxFrameFilteringCount = 1;
        rtParameters.reservoirRandBias = 0.0;
        rtParameters.reservoirSpacialRandBias = 0.2;
        rtParameters.spacialRadius = 64.0f;
        rtParameters.spacialSampleCount = 16;
        rtParameters.SHARCSceneScale = 20;
        rtParameters.SHARCEntriesNum = SHARCEntryCount;
        rtParameters.SHARCHashEntriesBufferIndex = SHARCHash.uav.offset;
        rtParameters.SHARCAccumulationBufferIndex = SHARCAccumulation.uav.offset;
        rtParameters.SHARCResolvedBufferIndex = SHARCResolved.uav.offset;
        rtParameters.SHARCAccumulationFrameNum = 128;
        rtParameters.SHARCStaleFrameNum = 256;
        rtParameters.SHARCEnableAntifirefly = true;
        rtParameters.SHARCSamplesPerPixel = 1;
        rtParameters.SHARCRadianceScale = 1;
        rtParameters.SHARCRoughnessThreshold = 0.33;

        rtParameters.enableBackFaceCull = true;
        rtParameters.enableLighting = true;
        rtParameters.enableTransmission = true;
        rtParameters.bouncesMax = 5;
        rtParameters.enableRussianRoulette = true;
        rtParameters.enableSoftShadows = true;
        rtParameters.throughputThreshold = 0.0001f;
        rtParameters.probeDownsampling = 6.0f;
    }

    void Off()
    {
        instancesRayTracing.Release();
        instancesRayTracingCount.Release();
        TLAS.Release();

        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            giReservoir.Get(i).Release();
        }
        SHARCHash.Release();
        SHARCAccumulation.Release();
        SHARCResolved.Release();
    }
};

struct SubmissionList;

class View
{
public:
    uint frame;
    uint2 renderResolution;
    uint2 displayResolution;
    HLSL::Upscaling upscaling;
    ViewWorld viewWorld;
    RayTracingContext raytracingContext;
    ViewContext viewContext;
    EditorContext editorContext;
    std::map<UINT64, Resource> resources;
    SubmissionList* submissions = nullptr; // owned by Renderer, shared by every view so passes on the same queue stay strictly ordered

    virtual void On(uint2 _displayResolution, uint2 _renderResolution)
    {
        displayResolution = _displayResolution;
        renderResolution = _renderResolution;
        frame = 0;
        viewWorld.On();
        raytracingContext.On(renderResolution);
        viewContext.On();
        editorContext.On();
    }
    virtual void Off()
    {
        viewWorld.Off();
        raytracingContext.Off();
        viewContext.Off();
        editorContext.Off();

        for (auto& item : resources)
        {
            item.second.Release();
        }
    }
    virtual tf::Task Schedule(World& world, tf::Subflow& subflow) = 0;

    Resource& GetRegisteredResource(String name)
    {
        UINT64 hash = std::hash<std::string>{}(name);
        Resource& res = resources[hash];
        return res;
    }
    
    HLSL::CommonResourcesIndices SetupCommonResourcesParams()
    {
        HLSL::CommonResourcesIndices commonResourcesIndices;

        commonResourcesIndices.meshesHeapIndex = MeshStorage::instance->meshes.srv.offset;
        commonResourcesIndices.meshCount = MeshStorage::instance->nextMeshOffset;
        commonResourcesIndices.meshletsHeapIndex = MeshStorage::instance->meshlets.srv.offset;
        commonResourcesIndices.meshletCount = MeshStorage::instance->nextMeshletOffset;
        commonResourcesIndices.meshletVerticesHeapIndex = MeshStorage::instance->meshletVertices.srv.offset;
        commonResourcesIndices.meshletVertexCount = MeshStorage::instance->nextMeshletVertexOffset;
        commonResourcesIndices.meshletTrianglesHeapIndex = MeshStorage::instance->meshletTriangles.srv.offset;
        commonResourcesIndices.meshletTriangleCount = MeshStorage::instance->nextMeshletTriangleOffset;
        commonResourcesIndices.verticesHeapIndex = MeshStorage::instance->vertices.srv.offset;
        commonResourcesIndices.vertexCount = MeshStorage::instance->nextVertexOffset;
        commonResourcesIndices.indicesHeapIndex = MeshStorage::instance->indices.srv.offset;
        commonResourcesIndices.indexCount = MeshStorage::instance->nextIndexOffset;
        commonResourcesIndices.camerasHeapIndex = viewWorld.cameras->gpuData.srv.offset;
        commonResourcesIndices.cameraCount = viewWorld.cameras->Size();
        commonResourcesIndices.lightsHeapIndex = viewWorld.lights->gpuData.srv.offset;
        commonResourcesIndices.lightCount = viewWorld.lights->Size();
        commonResourcesIndices.materialsHeapIndex = viewWorld.materials.GetResource().srv.offset;
        commonResourcesIndices.materialCount = viewWorld.materials.Size();
        commonResourcesIndices.instancesHeapIndex = viewWorld.instances.GetResource().srv.offset; // read-only StructuredBuffer<HLSL::Instance> everywhere it's bound, never RW
        commonResourcesIndices.instanceCount = viewWorld.instances.Size();


        return commonResourcesIndices;
    }
    
    HLSL::ViewContext SetupViewContextParams()
    {
        frame++;
        HLSL::ViewContext viewContextParams;

        viewContextParams.renderResolution = float4(float(renderResolution.x), float(renderResolution.y), 1.0f / renderResolution.x, 1.0f / renderResolution.y);
        viewContextParams.displayResolution = float4(float(displayResolution.x), float(displayResolution.y), 1.0f / displayResolution.x, 1.0f / displayResolution.y);
        viewContextParams.upscaling = upscaling;
        viewContextParams.frameNumber = frame;
        if (IOs::instance->keys.pressed[VK_R])
            viewContextParams.frameNumber = 0;
        viewContextParams.frameTime = (uint)(Time::instance->currentTicks);
        viewContextParams.cameraIndex = options.stopFrustumUpdate ? 1 : 0;
        viewContextParams.lightsIndex = 0;
        viewContextParams.instancesCulledArgsIndex = viewContext.instancesCulledArgs.GetResource().uav.offset;
        viewContextParams.meshletsToCullIndex = viewContext.meshletsToCull.GetResource().uav.offset;
        viewContextParams.instancesCounterIndex = viewContext.instancesCounter.GetResource().uav.offset;
        viewContextParams.meshletsCulledArgsIndex = viewContext.meshletsCulledArgs.GetResource().uav.offset;
        viewContextParams.meshletsCulledArgsSortedIndex = viewContext.meshletsCulledArgsSorted.GetResource().uav.offset;
        viewContextParams.sortHistogramIndex = viewContext.sortHistogram.GetResource().uav.offset;
        viewContextParams.meshletBucketsIndex = viewContext.meshletBuckets.GetResource().uav.offset;
        viewContextParams.frontToBackSort = options.frontToBackSort ? 1 : 0;
        viewContextParams.meshletsCounterIndex = viewContext.meshletsCounter.GetResource().uav.offset;
        viewContextParams.shaderBucketOffsetsIndex = viewWorld.shaderBucketOffsets.Get().gpuData.srv.offset;
        viewContextParams.shaderBucketCount = (uint)viewWorld.shaderBuckets.size();
        viewContextParams.albedoIndex = GetRegisteredResource("albedo").srv.offset;
        viewContextParams.metalnessIndex = GetRegisteredResource("metalness").srv.offset;
        viewContextParams.normalIndex = GetRegisteredResource("normal").srv.offset;
        viewContextParams.normalUAVIndex = GetRegisteredResource("normal").uav.offset;
        viewContextParams.motionIndex = GetRegisteredResource("motion").srv.offset;
        viewContextParams.instanceIDIndex = GetRegisteredResource("instanceID").srv.offset; // read-only Texture2D<uint> in debugBuffers.hlsl/selection.hlsl, never RW
        viewContextParams.overdrawIndex = GetRegisteredResource("overdraw").uav.offset;
        viewContextParams.depthIndex = GetRegisteredResource("depth").srv.offset;
        viewContextParams.reverseZ = true;
        viewContextParams.HZB = GetRegisteredResource("depthDownSample").srv.offset;
        viewContextParams.HZBMipCount = GetRegisteredResource("depthDownSample").GetResource()->GetDesc().MipLevels;
        viewContextParams.textureLODBias = -1.0f;
        viewContextParams.sortMaxDistance = options.sortMaxDistance;
        viewContextParams.lodDistanceMultiplier = options.lodDistanceMultiplier;
        viewContextParams.distanceCullingValue = options.distanceCullingValue;
        viewContextParams.distanceCullingValueRT = options.distanceCullingValueRT;
        viewContextParams.mousePixel = int4(IOs::instance->mouse.mousePos[0], IOs::instance->mouse.mousePos[1], IOs::instance->mouse.mousePos[2], IOs::instance->mouse.mousePos[3]);
        float2 previousJit = ((viewContext.jitter[viewContext.jitterIndex] - float2(0.5f, 0.5f)) / viewContextParams.renderResolution.xy);
        viewContext.jitterIndex = (viewContextParams.frameNumber) % ARRAYSIZE(viewContext.jitter);
        float2 jit = ((viewContext.jitter[viewContext.jitterIndex] - float2(0.5f, 0.5f)) / viewContextParams.renderResolution.xy);
        viewContextParams.jitter = float4(jit.x, jit.y, previousJit.x, previousJit.y);
        
        return viewContextParams;
    }

    HLSL::RTParameters SetupRayTracingContextParams()
    {
        HLSL::RTParameters rayTracingContextParams = raytracingContext.rtParameters;

        rayTracingContextParams.BVH = raytracingContext.TLAS.srv.offset;
        rayTracingContextParams.giReservoirIndex = raytracingContext.giReservoir.Get().uav.offset;
        rayTracingContextParams.previousgiReservoirIndex = raytracingContext.giReservoir.GetPrevious().uav.offset;
        rayTracingContextParams.lightedIndex = GetRegisteredResource("lighted").uav.offset;
        rayTracingContextParams.specularHitDistanceIndex = GetRegisteredResource("specularHitDistance").uav.offset;
        rayTracingContextParams.instancesRaytracingHeapIndex = raytracingContext.instancesRayTracing.GetResource().uav.offset;
        rayTracingContextParams.instancesRaytracingCountHeapIndex = raytracingContext.instancesRayTracingCount.GetResource().uav.offset;

        return rayTracingContextParams;
    }

    HLSL::EditorContext SetupEditorParams()
    {
        HLSL::EditorContext editorContextParams;

        editorContextParams.debugFlags =
            (options.debugMode == Options::DebugMode::ray ? HLSL::EditorDebugRays : 0)
            | (options.debugMode == Options::DebugMode::boundingSphere ? HLSL::EditorDebugBoundingVolumes : 0)
            | (options.debugDraw == Options::DebugDraw::albedo ? HLSL::EditorDebugAlbedo : 0)
            | (options.debugDraw == Options::DebugDraw::normals ? HLSL::EditorDebugNormals : 0)
            | (options.debugDraw == Options::DebugDraw::clusters ? HLSL::EditorDebugClusters : 0)
            | (options.debugDraw == Options::DebugDraw::triangles ? HLSL::EditorDebugTriangles : 0)
            | (options.debugDraw == Options::DebugDraw::lighting ? HLSL::EditorDebugLighting : 0)
            | (options.debugDraw == Options::DebugDraw::GIprobes ? HLSL::EditorDebugGIprobes : 0)
            | (options.debugDraw == Options::DebugDraw::GIBounces ? HLSL::EditorDebugGIBounces : 0)
            | (options.debugDraw == Options::DebugDraw::GIAlbedo ? HLSL::EditorDebugGIAlbedo : 0)
            | (options.debugDraw == Options::DebugDraw::GINormals ? HLSL::EditorDebugGINormals : 0)
            | (options.debugDraw == Options::DebugDraw::overdraw ? HLSL::EditorDebugOverdraw : 0);
        editorContextParams.debugBufferHeapIndex = editorContext.indirectDebugBuffer.GetResource().uav.offset;
        editorContextParams.debugVerticesHeapIndex = editorContext.indirectDebugVertices.GetResource().uav.offset;
        editorContextParams.debugVerticesCountHeapIndex = editorContext.indirectDebugVerticesCount.GetResource().uav.offset;
        editorContextParams.selectionResultIndex = editorContext.selectionResult.GetResource().uav.offset;

        return editorContextParams;
    }

};

class ViewResource
{
    static std::mutex lock;
    View* view = nullptr;
    UINT64 hash = 0;
public:
    void Register(std::string _name, View* _view)
    {
        view = _view;
        hash = std::hash<std::string>{}(_name);
        lock.lock();
        if(!view->resources.contains(hash))
            view->resources[hash] = Resource();
        lock.unlock();
    };
    void Unregister()
    {
        if (view == nullptr)
            return;
        lock.lock();
        auto it = view->resources.find(hash);
        if (it != view->resources.end())
        {
            it->second.Release();
            view->resources.erase(it);
        }
        lock.unlock();
    }
    Resource& Get()
    {
        lock.lock();
        auto it = view->resources.find(hash);
        // The hash must still be registered (created, and not yet Unregister'd). seedAssert is a
        // no-op in release, so fall back to operator[] there to avoid dereferencing end().
        seedAssert(it != view->resources.end());
        Resource& res = (it != view->resources.end()) ? it->second : view->resources[hash];
        lock.unlock();
        return res;
    }
};
std::mutex ViewResource::lock;

struct SubmissionList
{
    std::vector<std::function<void()>> execute; // submission action per slot, in order
    std::vector<bool> ready;                    // guarded by mutex
    size_t cursor = 0;
    std::mutex mutex;

    void Clear() // before each (re)build of the views
    {
        std::lock_guard lk(mutex);
        execute.clear();
        ready.clear();
        cursor = 0;
    }
    int Register(std::function<void()> fn) // at view (re)build, returns the slot index
    {
        execute.push_back(std::move(fn));
        ready.push_back(false);
        return (int)execute.size() - 1;
    }
    void Reset() // once per frame, before any drain
    {
        std::lock_guard lk(mutex);
        std::fill(ready.begin(), ready.end(), false);
        cursor = 0;
    }
    void MarkReadyAndDrain(int index) // called by whoever finishes a pass
    {
        std::lock_guard lk(mutex);
        ready[index] = true;
        while (cursor < execute.size() && ready[cursor])
            execute[cursor++]();
    }
    void DrainRemaining() // final safety net
    {
        std::lock_guard lk(mutex);
        while (cursor < execute.size() && ready[cursor])
            execute[cursor++]();
    }
};

class Pass
{
public:
    View* view;
    PerFrame<CommandBuffer> commandBuffer;
    PerFrame<CommandBuffer>* dependency = nullptr;
    PerFrame<CommandBuffer>* dependency2 = nullptr;

    // Slot in SubmissionList; the On() call order across the views defines the GPU submission order.
    int submitIndex = -1;

    // debug only ?
    String name;

    virtual void On(View* _view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2)
    {
        ZoneScoped;
        view = _view;
        dependency = _dependency;
        dependency2 = _dependency2;
        name = _name;
        //name = CharToWString(typeid(this).name()); // name = "class Pass * __ptr64"

        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i).On(queue, name);
        }

        submitIndex = view->submissions->Register([this] { this->Execute(); });
    }

    virtual void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i).Off();
        }
    }

    void Open()
    {
        ZoneScoped;

        auto hr = commandBuffer->cmdAlloc->Reset();
        commandBuffer->open = true;
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
        }
        hr = commandBuffer->cmd->Reset(commandBuffer->cmdAlloc, nullptr);
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
        }

#ifdef USE_PIX
        PIXBeginEvent(commandBuffer->cmd, PIX_COLOR_INDEX((BYTE)(UINT64)name.c_str()), name.c_str());
#endif
        Profiler::instance->StartProfile(commandBuffer.Get(), name.c_str());
    }

    void Close()
    {
        ZoneScoped;

        Profiler::instance->EndProfile(commandBuffer.Get());
#ifdef USE_PIX
        PIXEndEvent(commandBuffer->cmd);
#endif
        auto hr = commandBuffer->cmd->Close();
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
        }
        commandBuffer->open = false;
    }

    void Execute()
    {
        ZoneScoped;
        ZoneName(name.c_str(), name.size()); // show the pass name (e.g. "culling") instead of "Execute"

        if (dependency != nullptr)
        {
            commandBuffer->queue->Wait(dependency->Get().passEnd.fence, dependency->Get().passEnd.fenceValue);
            if (dependency2 != nullptr)
            {
                commandBuffer->queue->Wait(dependency2->Get().passEnd.fence, dependency2->Get().passEnd.fenceValue);
            }
        }
        else if(endOfLastFrame != nullptr)
        {
            uint lastFrameIndex = GPU::instance->frameIndex ? 0 : 1;
            commandBuffer->queue->Wait(endOfLastFrame->Get(lastFrameIndex).passEnd.fence, endOfLastFrame->Get(lastFrameIndex).passEnd.fenceValue);
        }
        ExecuteNow();
    }

    void ExecuteNow()
    {
        ZoneScoped;
        if (commandBuffer->open)
            IOs::Log("{} OPEN !!", name.c_str());

        commandBuffer->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&commandBuffer->cmd);
        commandBuffer->queue->Signal(commandBuffer->passEnd.fence, ++commandBuffer->passEnd.fenceValue);
    }

    void SetupView(View* view, Resource* RT, uint RTCount, bool clearRT, Resource* depth, bool clearDepth, bool displayResolution)
    {
        ZoneScoped;
        UINT64 w = view->renderResolution.x;
        UINT64 h = view->renderResolution.y;

        if (displayResolution)
        {
            w = view->displayResolution.x;
            h = view->displayResolution.y;
        }

        float4 panScale(0.0f, 0.0f, 1.0f, 1.0f);

        D3D12_VIEWPORT vp = {};
        vp.TopLeftX = w * panScale.x;
        vp.TopLeftY = h * panScale.y;
        vp.Width = w * panScale.z;
        vp.Height = h * panScale.w;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        commandBuffer->cmd->RSSetViewports(1, &vp);

        D3D12_RECT rect = {};
        rect.left = (LONG)vp.TopLeftX;
        rect.top = (LONG)vp.TopLeftY;
        rect.right = (LONG)(vp.TopLeftX + vp.Width);
        rect.bottom = (LONG)(vp.TopLeftY + vp.Height);
        commandBuffer->cmd->RSSetScissorRects(1, &rect);

        float4 clearColor(0.0f, 0.0f, 0.0f, 0.0f);
        // USE : commandBuffer->cmd->BeginRenderPass(); ?
        D3D12_CPU_DESCRIPTOR_HANDLE RTs[8] = {};
        for (uint i = 0; i < RTCount; i++)
        {
            RTs[i] = RT[i].rtv.handle;
            if(clearRT)
                commandBuffer->cmd->ClearRenderTargetView(RTs[i], clearColor.f32, 1, &rect);
        }

        commandBuffer->cmd->OMSetRenderTargets(RTCount, RTs, true, depth ? &depth->dsv.handle : nullptr);

        if (depth)
        {
            float clearDepthValue(1.0f);
            if (HLSL::reverseZ) clearDepthValue = 0;
            UINT8 clearStencilValue(0);
            if(clearDepth)
                commandBuffer->cmd->ClearDepthStencilView(depth->dsv.handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clearDepthValue, clearStencilValue, 1, &rect);
        }
    }

    virtual void Setup(View* view) = 0;
    virtual void Render(View* view) = 0;
};

class StructuredCommandBufferUpdate : public Pass
{

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        view->viewWorld.materials.Upload(commandBuffer.Get());
        view->viewWorld.instances.Upload(commandBuffer.Get());
        if (options.enableStructuredCommandBuffersReadback && view->viewWorld.instances.GetResource().GetResource() != nullptr)
        {
            uint elementCount = view->viewWorld.instances.ReadBack(commandBuffer.Get());

            HLSL::Instance* readBackInstances = nullptr;
            view->viewWorld.instances.ReadBackMap((void**)&readBackInstances);
            std::vector<HLSL::Instance> instances(readBackInstances, readBackInstances + elementCount);
            view->viewWorld.instancesReadBackDebug = instances;
            view->viewWorld.instances.ReadBackUnMap();
        }

        Close();
    }
};

class Skinning : public Pass
{
public:
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        // no need for culling context
        // just loop on all meshes of the renderer world that need skinning
        Open();
        Close();
    }
};

class Particles : public Pass
{
public:
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;

        Open();
        Close();
    }
};

class Spawning : public Pass
{
public:
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;

        Open();
        Close();
    }
};

class AccelerationStructure : public Pass
{
public:
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();
        uint instanceCount = view->viewWorld.instances.Size();

        if (instanceCount == 0)
        {
            Close();
            return;
        }

        bool allowUpdate = true;

        //CPU stuff ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

        // The generated AS can support iterative updates. This may change the final
        // size of the AS as well as the temporary memory requirements, and hence has
        // to be set before the actual build
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = allowUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
            : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        // Describe the work being requested, in this case the construction of a
        // (possibly dynamic) top-level hierarchy, with the given instance descriptors
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildDesc = {};
        prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        prebuildDesc.NumDescs = instanceCount;
        prebuildDesc.Flags = flags;

        // This structure is used to hold the sizes of the required scratch memory and
        // resulting AS
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};

        // Building the acceleration structure (AS) requires some scratch space, as
        // well as space to store the resulting structure This function computes a
        // conservative estimate of the memory requirements for both, based on the
        // number of bottom-level instances.
        GPU::instance->device->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);

        // Buffer sizes need to be 256-byte-aligned
        uint scratchSizeInBytes = ROUND_UP(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        uint resultSizeInBytes = ROUND_UP(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        // The instance descriptors are stored as-is in GPU memory, so we can deduce
        // the required size from the instance count
        uint descriptorsSizeInBytes = ROUND_UP(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceCount, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);


        //GPU stuff ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

        bool updateOnly = false;
        // If this in an update operation we need to provide the source buffer
        D3D12_GPU_VIRTUAL_ADDRESS pSourceAS = updateOnly ? view->raytracingContext.TLAS.GetResource()->GetGPUVirtualAddress() : 0;

        // The stored flags represent whether the AS has been built for updates or
        // not. If yes and an update is requested, the builder is told to only update
        // the AS instead of fully rebuilding it
        if (flags == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE && updateOnly)
        {
            flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        }

        //seedAssert(view->raytracingContext.instancesRayTracing.Size() == instanceCount);
        seedAssert(view->raytracingContext.TLAS.GetResource()->GetDesc().Width >= descriptorsSizeInBytes);

        // Create a descriptor of the requested builder work, to generate a top-level
        // AS from the input parameters
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        buildDesc.Inputs.InstanceDescs = view->raytracingContext.instancesRayTracing.GetResourcePtr()->GetGPUVirtualAddress();
        buildDesc.Inputs.NumDescs = instanceCount;
        buildDesc.DestAccelerationStructureData = view->raytracingContext.TLAS.GetResource()->GetGPUVirtualAddress();
        buildDesc.ScratchAccelerationStructureData = MeshStorage::instance->scratchBLAS.GetResource()->GetGPUVirtualAddress();
        buildDesc.SourceAccelerationStructureData = pSourceAS;
        buildDesc.Inputs.Flags = flags;

        // Build the top-level AS
        commandBuffer->cmd->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        // Wait for the builder to complete by setting a barrier on the resulting
        // buffer. This can be important in case the rendering is triggered
        // immediately afterwards, without executing the command list
        D3D12_RESOURCE_BARRIER uavBarrier;
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = view->raytracingContext.TLAS.GetResource();
        uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        commandBuffer->cmd->ResourceBarrier(1, &uavBarrier);

        Close();
    }
};

class HZB : public Pass
{
    ViewResource depth;
    ViewResource depthDownSample;
    FfxSpdContextDescription initializationParameters = { 0 };
    FfxSpdContext            context;
public:
    virtual void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;

        depth.Register("depth", view);
        depthDownSample.Register("depthDownSample", view);
        depthDownSample.Get().CreateTexture(view->renderResolution, DXGI_FORMAT_R32_FLOAT, true, "depthDownSample");

        // create backend interface (DX12)
        size_t scratchBufferSize = ffxGetScratchMemorySizeDX12(1);
        void* scratchBuffer = malloc(scratchBufferSize);
        memset(scratchBuffer, 0, scratchBufferSize);
        ffxGetInterfaceDX12(&initializationParameters.backendInterface, ffxGetDeviceDX12(GPU::instance->device), scratchBuffer, scratchBufferSize, 1);

        // Setup all the parameters for this SPD run
        initializationParameters.flags = 0;   // Reset
        initializationParameters.flags |= FFX_SPD_SAMPLER_LOAD;
        initializationParameters.flags |= FFX_SPD_WAVE_INTEROP_LDS;
        initializationParameters.flags |= FFX_SPD_MATH_PACKED;
        initializationParameters.downsampleFilter = FFX_SPD_DOWNSAMPLE_FILTER_MAX;
        if (HLSL::reverseZ) initializationParameters.downsampleFilter = FFX_SPD_DOWNSAMPLE_FILTER_MIN;
        ffxSpdContextCreate(&context, &initializationParameters);

    }
    virtual void Off() override
    {
        Pass::Off();
        ZoneScoped;
        depthDownSample.Unregister();
        ffxSpdContextDestroy(&context);
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();
        if (options.stopFrustumUpdate)
        {
            Close();
            return;
        }

        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        depthDownSample.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION cpyLocSrc;
        cpyLocSrc.SubresourceIndex = 0;
        cpyLocSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cpyLocSrc.pResource = depth.Get().GetResource();
        D3D12_TEXTURE_COPY_LOCATION cpyLocDst;
        cpyLocDst.SubresourceIndex = 0;
        cpyLocDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cpyLocDst.pResource = depthDownSample.Get().GetResource();
        commandBuffer->cmd->CopyTextureRegion(&cpyLocDst, 0,0,0, &cpyLocSrc, nullptr);
        depthDownSample.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        FfxResource zbuff = {};
        zbuff.description = ffxGetResourceDescriptionDX12(depthDownSample.Get().GetResource());
        zbuff.state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ;
        zbuff.resource = depthDownSample.Get().GetResource();
        FfxSpdDispatchDescription dispatchParameters = {};
        dispatchParameters.commandList = commandBuffer->cmd;
        dispatchParameters.resource = zbuff;

        FfxErrorCode errorCode = ffxSpdContextDispatch(&context, &dispatchParameters);
        Close();
    }
};

class Culling : public Pass
{
    ViewResource depth;
    Components::Handle<Components::Shader> cullingResetShader;
    Components::Handle<Components::Shader> cullingInstancesShader;
    Components::Handle<Components::Shader> cullingCountMeshletsDispatchShader;
    Components::Handle<Components::Shader> cullingMeshletsShader;
    Components::Handle<Components::Shader> sortPrefixShader;
    Components::Handle<Components::Shader> sortScatterShader;
public:
    virtual void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        depth.Register("depth", view);
        cullingResetShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\culling.hlsl|Reset");
        cullingInstancesShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\culling.hlsl|Instances");
        cullingCountMeshletsDispatchShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\culling.hlsl|Count");
        cullingMeshletsShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\culling.hlsl|Meshlets");
        sortPrefixShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\culling.hlsl|SortPrefix");
        sortScatterShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\culling.hlsl|SortScatter");
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        view->viewContext.instancesCulledArgs.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
        view->viewContext.meshletsCulledArgs.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
        view->viewContext.meshletsCulledArgsSorted.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
        view->viewContext.instancesCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
        view->viewContext.meshletsCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);

        auto& instances = view->viewWorld.instances;
        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto raytracingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);

        Shader& reset = *AssetLibrary::instance->Get<Shader>(cullingResetShader.Get().id, true);
        commandBuffer->SetCompute(reset);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, raytracingContextAddress);
        commandBuffer->cmd->Dispatch(1, 1, 1);

        view->viewContext.instancesCounter.GetResource().Barrier(commandBuffer.Get());

        Shader& cullingInstances = *AssetLibrary::instance->Get<Shader>(cullingInstancesShader.Get().id, true);
        commandBuffer->SetCompute(cullingInstances);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, raytracingContextAddress);
        commandBuffer->cmd->Dispatch(cullingInstances.DispatchX(view->viewWorld.commonResourcesIndices.instanceCount), 1, 1);

        view->viewContext.instancesCounter.GetResource().Barrier(commandBuffer.Get());
        view->viewContext.instancesCulledArgs.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        view->viewContext.instancesCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        Shader& cullingCountMeshletsDispatch = *AssetLibrary::instance->Get<Shader>(cullingCountMeshletsDispatchShader.Get().id, true);
        commandBuffer->SetCompute(cullingCountMeshletsDispatch);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, raytracingContextAddress);
        commandBuffer->cmd->Dispatch(1, 1, 1);

        view->viewContext.instancesCulledArgs.GetResource().Barrier(commandBuffer.Get());

        Shader& cullingMeshlets = *AssetLibrary::instance->Get<Shader>(cullingMeshletsShader.Get().id, true);
        commandBuffer->SetCompute(cullingMeshlets);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, raytracingContextAddress);
        commandBuffer->cmd->ExecuteIndirect(cullingMeshlets.commandSignature, view->viewWorld.commonResourcesIndices.instanceCount, view->viewContext.instancesCulledArgs.GetResourcePtr(), 0, view->viewContext.instancesCounter.GetResourcePtr(), 0);

        if (options.frontToBackSort)
        {
            view->viewContext.meshletsCulledArgs.GetResource().Barrier(commandBuffer.Get());
            view->viewContext.meshletsCounter.GetResource().Barrier(commandBuffer.Get());
            view->viewContext.meshletBuckets.GetResource().Barrier(commandBuffer.Get());
            view->viewContext.sortHistogram.GetResource().Barrier(commandBuffer.Get());

            uint sortElements = view->viewContext.meshletsCulledArgs.Size();

            Shader& sortPrefix = *AssetLibrary::instance->Get<Shader>(sortPrefixShader.Get().id, true);
            commandBuffer->SetCompute(sortPrefix);
            commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
            commandBuffer->cmd->Dispatch(1, 1, 1);

            view->viewContext.sortHistogram.GetResource().Barrier(commandBuffer.Get());

            Shader& sortScatter = *AssetLibrary::instance->Get<Shader>(sortScatterShader.Get().id, true);
            commandBuffer->SetCompute(sortScatter);
            commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
            commandBuffer->cmd->Dispatch(sortScatter.DispatchX(sortElements), 1, 1);

            view->viewContext.meshletsCulledArgsSorted.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }

        view->viewContext.meshletsCulledArgs.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        view->viewContext.meshletsCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        Close();
    }
};

class ZPrepass : public Pass
{
    ViewResource depth;
public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        depth.Register("depth", view);
        depth.Get().CreateDepthTarget(view->renderResolution, "depth");

        Open();
        commandBuffer.Get().cmd->DiscardResource(depth.Get().GetResource(), nullptr);
        Close();
        ExecuteNow();
    }
    void Off() override
    {
        Pass::Off();
        ZoneScoped;
        depth.Unregister();
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    // Draw occluders ? no need if using previous frame Z ?
    void Render(View* view) override
    {
        ZoneScoped;
        Open();
        Close();
    }
};

class GBuffers : public Pass
{
    ViewResource albedo;
    ViewResource specularAlbedo;
    ViewResource normal;
    ViewResource metalness;
    ViewResource depth;
    ViewResource motion;
    ViewResource instanceID;
    ViewResource overdraw;
public:

    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        albedo.Register("albedo", view);
        albedo.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R8G8B8A8_UNORM, "albedo");
        specularAlbedo.Register("specularAlbedo", view);
        specularAlbedo.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R8G8B8A8_UNORM, "specularAlbedo");
        normal.Register("normal", view);
        normal.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R16G16B16A16_FLOAT, "normal"); // a = roughness (DLSS-RR packed mode)
        metalness.Register("metalness", view);
        metalness.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R8_UNORM, "metalness");
        depth.Register("depth", view);
        motion.Register("motion", view);
        motion.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R16G16_FLOAT, "motion");
        instanceID.Register("instanceID", view);
        instanceID.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R32_UINT, "instanceID");
        overdraw.Register("overdraw", view);
        overdraw.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R32_UINT, "overdraw"); // per-pixel atomic counter for the overdraw heatmap

        Open();
        albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        instanceID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandBuffer.Get().cmd->DiscardResource(albedo.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(specularAlbedo.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(normal.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(metalness.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(motion.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(instanceID.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(overdraw.Get().GetResource(), nullptr);
        albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        instanceID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        Close();
        ExecuteNow();
    }
    void Off() override
    {
        Pass::Off();
        ZoneScoped;
        albedo.Unregister();
        specularAlbedo.Unregister();
        normal.Unregister();
        metalness.Unregister();
        motion.Unregister();
        instanceID.Unregister();
        overdraw.Unregister();
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        instanceID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);

        bool overdrawEnabled = options.debugDraw == Options::DebugDraw::overdraw;
        if (overdrawEnabled)
        {
            float clearOverdraw[4] = { 0, 0, 0, 0 };
            overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
            commandBuffer->cmd->ClearRenderTargetView(overdraw.Get().rtv.handle, clearOverdraw, 0, nullptr);
            overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        Resource rts[] = { albedo.Get(), specularAlbedo.Get(), normal.Get(), metalness.Get(), motion.Get(), instanceID.Get()};
        SetupView(view, rts, ARRAYSIZE(rts), true, &depth.Get(), true, false);

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);

        for (uint bucketIndex = 0; bucketIndex < view->viewWorld.shaderBuckets.size(); bucketIndex++)
        {
            ShaderBucketInfo& bucket = view->viewWorld.shaderBuckets[bucketIndex];
            if (bucket.meshletCount == 0)
                continue;

            Shader* shader = AssetLibrary::instance->Get<Shader>(bucket.shaderAssetId, true);
            if (!shader)
                continue;

            commandBuffer->SetGraphic(*shader);
            commandBuffer->cmd->SetGraphicsRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
            commandBuffer->cmd->SetGraphicsRootConstantBufferView(ViewContextRegister, viewContextAddress);
            commandBuffer->cmd->SetGraphicsRootConstantBufferView(EditorContextRegister, editorContextAddress);

            uint counterOffset = bucketIndex * (uint)sizeof(uint);
            if (bucketIndex == 0 && options.frontToBackSort)
            {
                auto& sortedArgs = view->viewContext.meshletsCulledArgsSorted;
                commandBuffer->cmd->ExecuteIndirect(shader->commandSignature, bucket.meshletCount, sortedArgs.GetResourcePtr(), 0, view->viewContext.meshletsCounter.GetResourcePtr(), counterOffset);
            }
            else
            {
                auto& drawArgs = view->viewContext.meshletsCulledArgs;
                commandBuffer->cmd->ExecuteIndirect(shader->commandSignature, bucket.meshletCount, drawArgs.GetResourcePtr(), (UINT64)bucket.baseOffset * sizeof(HLSL::MeshletDrawCall), view->viewContext.meshletsCounter.GetResourcePtr(), counterOffset);
            }
        }

        albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        instanceID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

        if (overdrawEnabled)
            overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON);

        Close();
    }
};

class LightingProbes : public Pass
{
    Components::Handle<Components::Shader> rayDispatchShader;
    Components::Handle<Components::Shader> resolveShader;
    ViewResource depth;

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;

        rayDispatchShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\raytracing2.hlsl|Update");
        resolveShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\SHARCResolve.hlsl|sharcResolve");
        depth.Register("depth", view);

    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);
        auto raytracingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);

        // GBuffers::Render leaves depth in COMMON; this dispatch reads it (GetGBufferCameraData) as a
        // plain Texture2D SRV, which for a DEPTH_STENCIL-flagged resource is never implicitly promoted.
        // Returned to COMMON right after so Lighting::Render (next consumer) finds it where it expects.
        // NOTE: this pass runs on the COMPUTE queue -- DEPTH_READ/DEPTH_WRITE aren't legal resource
        // states on a compute command list (only DIRECT lists touch the depth-stencil pipeline stage),
        // so this is NON_PIXEL_SHADER_RESOURCE alone, unlike the DEPTH_READ-combined bracket used for
        // this same resource in the graphics-queue passes (Lighting, PostProcessHalfRes, GPUDebug, DLSS).
        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // Trace rays with updatte defined (very low resolution, only for SHARC update)
        Shader& rayDispatch = *AssetLibrary::instance->Get<Shader>(rayDispatchShader.Get().id, true);
        commandBuffer->SetRaytracing(rayDispatch);
        // global root sig for ray tracing is the same as compute shaders
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, raytracingContextAddress);

        D3D12_DISPATCH_RAYS_DESC drd = rayDispatch.GetRTDesc();
        drd.Width = (uint)((float)view->renderResolution.x / view->raytracingContext.rtParameters.probeDownsampling);
        drd.Height = (uint)((float)view->renderResolution.y / view->raytracingContext.rtParameters.probeDownsampling);

        commandBuffer->cmd->DispatchRays(&drd);

        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);

        view->raytracingContext.SHARCAccumulation.Barrier(commandBuffer.Get());

        // close the "lightingProbes" zone (SHARC update trace) and time the resolve separately
        Profiler::instance->EndProfile(commandBuffer.Get());
        Profiler::instance->StartProfile(commandBuffer.Get(), "sharcResolve");

        Shader& resolve = *AssetLibrary::instance->Get<Shader>(resolveShader.Get().id, true);
        commandBuffer->SetCompute(resolve);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, raytracingContextAddress);

        commandBuffer->cmd->Dispatch(resolve.DispatchX(view->raytracingContext.rtParameters.SHARCEntriesNum), 1, 1);

        Close();
    }
};

class Lighting : public Pass
{
    ViewResource lighted;
    ViewResource specularHitDistance;
    ViewResource albedo;
    ViewResource depth;
    ViewResource normal;
    Components::Handle<Components::Shader> rayDispatchShader;
    Components::Handle<Components::Shader> applyLightingShader;

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        lighted.Register("lighted", view);
        lighted.Get().CreateTexture(view->renderResolution, DXGI_FORMAT_R16G16B16A16_FLOAT, false, "lighted");
        specularHitDistance.Register("specularHitDistance", view);
        specularHitDistance.Get().CreateTexture(view->renderResolution, DXGI_FORMAT_R16_FLOAT, false, "specularHitDistance");
        albedo.Register("albedo", view);
        depth.Register("depth", view);
        normal.Register("normal", view);
        rayDispatchShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\raytracing2.hlsl|Query");
        applyLightingShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\lighting.hlsl|Lighting");

        Open();
        lighted.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        specularHitDistance.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Close();
        ExecuteNow();
    }
    void Off() override
    {
        Pass::Off();
        ZoneScoped;
        lighted.Unregister();
        specularHitDistance.Unregister();
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);
        auto raytracingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);

        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // Trace rays (+ temporal ReSTIR)
        Shader& rayDispatch = *AssetLibrary::instance->Get<Shader>(rayDispatchShader.Get().id, true);
        commandBuffer->SetRaytracing(rayDispatch);
        // global root sig for ray tracing is the same as compute shaders
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, raytracingContextAddress);
        D3D12_DISPATCH_RAYS_DESC drd = rayDispatch.GetRTDesc();
        drd.Width = view->renderResolution.x;
        drd.Height = view->renderResolution.y;
        commandBuffer->cmd->DispatchRays(&drd);

        view->raytracingContext.giReservoir.Get().Barrier(commandBuffer.Get());

        // close the "lighting" zone (trace + temporal ReSTIR) and time the second dispatch separately
        Profiler::instance->EndProfile(commandBuffer.Get());
        Profiler::instance->StartProfile(commandBuffer.Get(), "lightingApply");

        // Lighting (+ spacial ResTIR). Also packs roughness into normal.a for DLSS-RR
        // (lighting.hlsl:284-285, RWTexture2D write) -- GBuffers::Render leaves normal in COMMON
        // (its own SRV consumers rely on implicit promotion, fine), but a UAV write always needs an
        // explicit transition regardless of creation flags.
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        Shader& applyLighting = *AssetLibrary::instance->Get<Shader>(applyLightingShader.Get().id, true);
        commandBuffer->SetRaytracing(applyLighting);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters));
        drd = applyLighting.GetRTDesc();
        drd.Width = view->renderResolution.x;
        drd.Height = view->renderResolution.y;
        commandBuffer->cmd->DispatchRays(&drd);

        lighted.Get().Barrier(commandBuffer.Get());
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        Close();
    }
};

class GPUDebugInit : public Pass
{
    Components::Handle<Components::Shader> indirectDebugInitShader;

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        indirectDebugInitShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\debug.hlsl|DebugInit");
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);

        Shader& debugInit = *AssetLibrary::instance->Get<Shader>(indirectDebugInitShader.Get().id, true);
        commandBuffer->SetCompute(debugInit);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
        commandBuffer->cmd->Dispatch(1, 1, 1);

        Close();
    }
};

class GPUDebug : public Pass
{
    ViewResource lighted;
    ViewResource depth;
    ViewResource overdraw;
    Components::Handle<Components::Shader> indirectDebugShader;
    Components::Handle<Components::Shader> selectionShader;
    Components::Handle<Components::Shader> debugBuffersShader;

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        lighted.Register("lighted", view);
        depth.Register("depth", view);
        overdraw.Register("overdraw", view);
        indirectDebugShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\debug.hlsl|Debug");
        selectionShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\selection.hlsl|Selection");
        debugBuffersShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\debugBuffers.hlsl|Lighting");
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);
        auto rtParametersAddress = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);


        Shader& selection = *AssetLibrary::instance->Get<Shader>(selectionShader.Get().id, true);
        commandBuffer->SetCompute(selection);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, rtParametersAddress);

        commandBuffer->cmd->Dispatch(1, 1, 1);

        view->editorContext.selectionResult.ReadBack(commandBuffer.Get());

        //move the reading of objectID outside graphic code ?
        if (IOs::instance->mouse.mouseButtonLeftUp && !IOs::instance->mouse.mouseDrag)
        {
            uint* selectionResult = nullptr;
            view->editorContext.selectionResult.ReadBackMap((void**)&selectionResult);
            editorState.selectedObject.FromUInt(selectionResult[0]);
            view->editorContext.selectionResult.ReadBackUnMap();
        }

        if (options.debugMode != Options::DebugMode::none
            || options.debugDraw != Options::DebugDraw::none)
        {
            // debugBuffers.hlsl's Lighting entry unconditionally calls GetGBufferCameraData, which
            // reads depth as SRV -- by this point in the frame Lighting::Render has already put it
            // back in DEPTH_WRITE for next frame's zPrepass, so it needs its own bracket here too.
            depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            bool overdrawDebug = options.debugDraw == Options::DebugDraw::overdraw;
            if (overdrawDebug)
                overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            Shader& debugBuffers = *AssetLibrary::instance->Get<Shader>(debugBuffersShader.Get().id, true);
            commandBuffer->SetCompute(debugBuffers);
            commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, rtParametersAddress);
            commandBuffer->cmd->Dispatch(debugBuffers.DispatchX(view->renderResolution.x), debugBuffers.DispatchY(view->renderResolution.y), 1);

            if (overdrawDebug)
                overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

            if (options.debugMode == Options::DebugMode::ray
                || options.debugMode == Options::DebugMode::boundingSphere)
            {
                Resource rts[] = { lighted.Get() };
                SetupView(view, rts, ARRAYSIZE(rts), false, &depth.Get(), false, false);
                //SetupView(view, rts, ARRAYSIZE(rts), false, nullptr, false, true);

                lighted.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);

                Shader& indirectDebug = *AssetLibrary::instance->Get<Shader>(indirectDebugShader.Get().id, true);
                commandBuffer->SetGraphic(indirectDebug);
                commandBuffer->cmd->SetGraphicsRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
                commandBuffer->cmd->SetGraphicsRootConstantBufferView(ViewContextRegister, viewContextAddress);
                commandBuffer->cmd->SetGraphicsRootConstantBufferView(EditorContextRegister, editorContextAddress);

                uint maxDraw = 2;
                commandBuffer->cmd->ExecuteIndirect(indirectDebug.commandSignature, maxDraw, view->editorContext.indirectDebugBuffer.GetResourcePtr(), 0, view->editorContext.indirectDebugVerticesCount.GetResourcePtr(), 0);

                lighted.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }

        Close();
    }
};

class Forward : public Pass
{
    ViewResource lighted;
    ViewResource depth;
    Components::Handle<Components::Shader> meshShader;

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        depth.Register("depth", view);
        lighted.Register("lighted", view);
        meshShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\mesh.hlsl|DefaultF");
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        Close();
    }
};

class TerrainErosion : public Pass
{
    Components::Handle<Components::Shader> erosionShader;
    Components::Handle<Components::Shader> bakeShader;
    Components::Handle<Components::Shader> noiseShader;
public:
    static constexpr uint ErodedResolution = 8192; // 8k R16_UNORM + R8_UNORM = 192 MiB of VRAM PER TERRAIN

    struct ErodedMaps
    {
        Resource height; // R16_UNORM eroded heightmap (mesh displacement)
        Resource diff;   // R8_UNORM difference vs the original map, 0.5 = untouched (albedo debug/blending)
        assetID bakedInput = assetID::Invalid; // input heightmap of the last completed bake, for the bake-once (erosionEnabled == 1) skip
        uint bakedProceduralGeneration = 0;    // same role as bakedInput when the base is procedural (no assetID to compare)
    };
    std::unordered_map<uint, ErodedMaps> erodedMaps;
    // Baking is dirty-triggered (footprint change, first attach, erosionEnabled==2), so editing
    // heightmapBlurRadius alone wouldn't re-bake already-baked nodes -- detect the change here and
    // force one full re-bake of every node of that terrain the frame it changes (see forceRebakeAll
    // below), so tuning it in the editor actually updates without needing another trigger.
    std::unordered_map<uint, uint> lastBlurRadius;

    // Procedural base heightmap (terrainErosion.hlsl's TerrainNoiseMain, an FBM sum of
    // PerlinNoise3 octaves), used in place of an imported heightmap texture when
    // Components::Terrain::useProceduralHeightmap is set. Regenerated only when its params
    // change (NoiseSnapshot), not every frame -- proceduralGeneration is bumped on each
    // regeneration so the erosion bake-once skip (below) knows to re-run when the base changes.
    struct NoiseSnapshot
    {
        float scale = 0, lacunarity = 0, gain = 0;
        uint octaves = 0, seed = 0;
        bool operator==(const NoiseSnapshot& o) const
        {
            return scale == o.scale && lacunarity == o.lacunarity && gain == o.gain && octaves == o.octaves && seed == o.seed;
        }
    };
    std::unordered_map<uint, Resource> proceduralHeightmaps;
    std::unordered_map<uint, NoiseSnapshot> lastNoiseParams;
    std::unordered_map<uint, uint> proceduralGeneration;

    virtual void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        erosionShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\terrainErosion.hlsl|Erosion");
        bakeShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\terrainMeshBake.hlsl|Bake");
        noiseShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\terrainErosion.hlsl|Noise");
    }
    void Off() override
    {
        for (auto& erodedMap : erodedMaps)
        {
            erodedMap.second.height.Release();
            erodedMap.second.diff.Release();
        }
        erodedMaps.clear();
        for (auto& map : proceduralHeightmaps)
            map.second.Release();
        proceduralHeightmaps.clear();
        Pass::Off();
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        std::vector<uint> aliveTerrains;    // terrains that keep their eroded map this frame
        std::vector<uint> aliveProcedural;  // terrains that keep their procedural base map this frame
        World& world = *World::instance;
        uint queryIndex = world.Query(Components::Terrain::mask, 0, true);
        auto& queryResult = world.frameQueries[queryIndex];

        // Pushed once, reused by both the erosion dispatches below and the bake dispatches
        // further down (previously computed per-terrain, inside the eroding branch only).
        D3D12_GPU_VIRTUAL_ADDRESS commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        D3D12_GPU_VIRTUAL_ADDRESS viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);

        // Per-terrain heightmap to sample for baking (eroded if available, else raw -- same
        // fallback terrainmesh.hlsl's live path applies), collected while erosion runs and
        // consumed by the node-vertex-bake loop below.
        struct TerrainBakeContext
        {
            uint heightmapSRV = HLSL::invalidUINT;
            uint erosionEnabled = 0;
            bool forceRebakeAll = false;
        };
        std::unordered_map<uint, TerrainBakeContext> bakeContexts;

        for (auto& eb : queryResult)
        {
            World::Entity e = eb;
            auto& terrainCmp = e.Get<Components::Terrain>();
            if (!terrainCmp.material.IsValid())
                continue; // nothing can consume the maps -> also skip the dispatch, the scan below frees them

            // Resolve the BASE heightmap SRV: either the imported texture, or a procedurally
            // generated perlin-noise map (terrainErosion.hlsl's TerrainNoiseMain), regenerated only
            // when its params change so steady state costs nothing.
            uint baseHeightmapSRV = HLSL::invalidUINT;
            if (terrainCmp.useProceduralHeightmap)
            {
                aliveProcedural.push_back(e.id);
                NoiseSnapshot snap;
                snap.scale = terrainCmp.noiseScale;
                snap.octaves = terrainCmp.noiseOctaves;
                snap.lacunarity = terrainCmp.noiseLacunarity;
                snap.gain = terrainCmp.noiseGain;
                snap.seed = terrainCmp.noiseSeed;

                Resource& map = proceduralHeightmaps[e.id];
                auto snapIt = lastNoiseParams.find(e.id);
                bool needsGen = map.GetResource() == nullptr || snapIt == lastNoiseParams.end() || !(snapIt->second == snap);
                if (needsGen)
                {
                    if (map.GetResource() == nullptr)
                        map.CreateTexture(uint2(ErodedResolution, ErodedResolution), DXGI_FORMAT_R16_UNORM, false, "terrainNoise_" + std::to_string(e.id));

                    Shader* noise = AssetLibrary::instance->Get<Shader>(noiseShader.Get().id, true);
                    HLSL::TerrainErosionParameters params = {};
                    params.outputResolution = ErodedResolution;
                    params.octaves = terrainCmp.noiseOctaves;
                    params.scale = terrainCmp.noiseScale;
                    params.lacunarity = terrainCmp.noiseLacunarity;
                    params.gain = terrainCmp.noiseGain;
                    params.seed = terrainCmp.noiseSeed;
                    params.outputHeightmapIndex = map.uav.offset;

                    map.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    commandBuffer->SetCompute(*noise);
                    commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
                    commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
                    commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&params));
                    commandBuffer->cmd->Dispatch(noise->DispatchX(ErodedResolution), noise->DispatchY(ErodedResolution), 1);
                    map.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

                    lastNoiseParams[e.id] = snap;
                    proceduralGeneration[e.id]++;
                }
                if (map.GetResource() != nullptr)
                    baseHeightmapSRV = map.srv.offset;
            }
            else if (terrainCmp.heightmap.IsValid())
            {
                Resource* raw = AssetLibrary::instance->Get<Resource>(terrainCmp.heightmap.Get().id);
                if (raw != nullptr)
                    baseHeightmapSRV = raw->srv.offset;
            }

            // erosionEnabled: 0 = off, 1 = bake once, 2 = bake continuously (see World.h)
            bool eroding = terrainCmp.erosionEnabled != 0 && baseHeightmapSRV != HLSL::invalidUINT;
            if (eroding)
            {
                aliveTerrains.push_back(e.id);
                ErodedMaps& maps = erodedMaps[e.id];

                // "Input changed" for the bake-once skip: an assetID swap for an imported
                // heightmap, or a fresh generation for a procedural one (no assetID to compare).
                bool inputIdentityChanged = terrainCmp.useProceduralHeightmap
                    ? (maps.bakedProceduralGeneration != proceduralGeneration[e.id])
                    : (maps.bakedInput != terrainCmp.heightmap.Get().id);

                if (!(terrainCmp.erosionEnabled == 1 && !inputIdentityChanged))
                {
                    Shader* erosion = AssetLibrary::instance->Get<Shader>(erosionShader.Get().id, true);

                    if (maps.height.GetResource() == nullptr)
                        maps.height.CreateTexture(uint2(ErodedResolution, ErodedResolution), DXGI_FORMAT_R16_UNORM, false, "terrainEroded_" + std::to_string(e.id));
                    if (maps.diff.GetResource() == nullptr)
                        maps.diff.CreateTexture(uint2(ErodedResolution, ErodedResolution), DXGI_FORMAT_R8_UNORM, false, "terrainErosionDiff_" + std::to_string(e.id));

                    HLSL::TerrainErosionParameters params = {};
                    params.inputHeightmapIndex = baseHeightmapSRV;
                    params.outputResolution = ErodedResolution;
                    params.octaves = terrainCmp.erosionOctaves;
                    params.scale = terrainCmp.erosionScale;
                    params.strength = terrainCmp.erosionStrength;
                    params.gullyWeight = terrainCmp.erosionGullyWeight;
                    params.detail = terrainCmp.erosionDetail;
                    params.lacunarity = terrainCmp.erosionLacunarity;
                    params.gain = terrainCmp.erosionGain;
                    params.cellScale = terrainCmp.erosionCellScale;
                    params.normalization = terrainCmp.erosionNormalization;
                    params.ridgeRounding = terrainCmp.erosionRidgeRounding;
                    params.creaseRounding = terrainCmp.erosionCreaseRounding;
                    params.outputHeightmapIndex = maps.height.uav.offset;
                    params.outputDiffIndex = maps.diff.uav.offset;
                    params.seed = 0;

                    maps.height.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    maps.diff.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    commandBuffer->SetCompute(*erosion);
                    commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
                    commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
                    commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&params));
                    commandBuffer->cmd->Dispatch(erosion->DispatchX(ErodedResolution), erosion->DispatchY(ErodedResolution), 1);
                    maps.height.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
                    maps.diff.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

                    // Handle<T>::Get() auto-vivifies (creates a real entity and rewrites the
                    // handle in place) when called on an invalid handle -- never call it on
                    // terrainCmp.heightmap in procedural mode, where it's deliberately left unset.
                    if (!terrainCmp.useProceduralHeightmap)
                        maps.bakedInput = terrainCmp.heightmap.Get().id;
                    maps.bakedProceduralGeneration = proceduralGeneration[e.id];
                }
            }

            // Resolves to the eroded map's SRV if erosion ran, else the procedural/imported base --
            // ctx.heightmapSRV below is what the bake dispatch actually samples.
            float erodedParam = -1.0f;
            auto mapsIt = erodedMaps.find(e.id);
            if (eroding && mapsIt != erodedMaps.end())
            {
                if (mapsIt->second.height.GetResource() != nullptr)
                    erodedParam = (float)mapsIt->second.height.srv.offset;
            }
            else if (terrainCmp.useProceduralHeightmap && baseHeightmapSRV != HLSL::invalidUINT)
            {
                erodedParam = (float)baseHeightmapSRV;
            }

            TerrainBakeContext ctx;
            ctx.erosionEnabled = terrainCmp.erosionEnabled;
            {
                auto blurIt = lastBlurRadius.find(e.id);
                ctx.forceRebakeAll = blurIt == lastBlurRadius.end() || blurIt->second != terrainCmp.heightmapBlurRadius;
                lastBlurRadius[e.id] = terrainCmp.heightmapBlurRadius;
            }
            ctx.heightmapSRV = erodedParam >= 0.0f ? (uint)erodedParam : baseHeightmapSRV;
            bakeContexts[e.id] = ctx;
        }

        for (auto it = erodedMaps.begin(); it != erodedMaps.end();)
        {
            if (std::find(aliveTerrains.begin(), aliveTerrains.end(), it->first) == aliveTerrains.end())
            {
                it->second.height.Release(true);
                it->second.diff.Release(true);
                it = erodedMaps.erase(it);
            }
            else
                ++it;
        }
        for (auto it = proceduralHeightmaps.begin(); it != proceduralHeightmaps.end();)
        {
            if (std::find(aliveProcedural.begin(), aliveProcedural.end(), it->first) == aliveProcedural.end())
            {
                it->second.Release(true);
                it = proceduralHeightmaps.erase(it);
            }
            else
                ++it;
        }

        // Terrain node vertex bake (MeshStorage::CreateMeshOverride / terrainMeshBake.hlsl): one
        // dispatch per node whose MeshOverride.dirty is set, or every node of a continuously-
        // eroding (mode 2) terrain. Runs after the erosion loop above so every terrain's
        // bakeContexts entry already reflects this frame's eroded map (or the raw fallback).
        std::vector<Mesh*> justBaked;
        {
            Shader* bake = nullptr; // fetched lazily: nothing to bake -> shader never loads/compiles
            uint overrideQueryIndex = world.Query(Components::MeshOverride::mask | Components::TerrainBaking::mask, 0, true);
            auto& overrideQueryResult = world.frameQueries[overrideQueryIndex];
            for (auto& oeb : overrideQueryResult)
            {
                World::Entity ent = oeb;
                auto& overrideCmp = ent.Get<Components::MeshOverride>();
                auto& bakingCmp = ent.Get<Components::TerrainBaking>();
                if (overrideCmp.overrideMeshIndex == ~0u || !bakingCmp.terrain.IsValid())
                    continue;

                auto ctxIt = bakeContexts.find(bakingCmp.terrain.id);
                if (ctxIt == bakeContexts.end() || ctxIt->second.heightmapSRV == HLSL::invalidUINT)
                    continue;

                bool needsBake = overrideCmp.dirty != 0 || ctxIt->second.erosionEnabled == 2 || ctxIt->second.forceRebakeAll;
                if (!needsBake)
                    continue;

                auto& instCmp = ent.Get<Components::Instance>();
                Components::Mesh& sourceMeshCmp = instCmp.mesh.Get();
                Mesh* sourceMesh = AssetLibrary::instance->Get<Mesh>(sourceMeshCmp.id);
                if (sourceMesh == nullptr)
                    continue;
                auto overrideIt = MeshStorage::instance->allMeshes.find(overrideCmp.overrideMeshIndex);
                if (overrideIt == MeshStorage::instance->allMeshes.end())
                    continue;
                Mesh& overrideMesh = overrideIt->second;

                Components::Terrain& terrainCmp = bakingCmp.terrain.Get();
                auto& tr = ent.Get<Components::Transform>();

                if (bake == nullptr)
                    bake = AssetLibrary::instance->Get<Shader>(bakeShader.Get().id, true);

                HLSL::TerrainBakeParameters params;
                params.verticesUAVIndex = MeshStorage::instance->vertices.uav.offset;
                params.heightmapIndex = ctxIt->second.heightmapSRV;
                params.sourceVertexOffset = sourceMesh->vertexOffset;
                params.outputVertexOffset = overrideMesh.vertexOffset;
                params.vertexCount = sourceMesh->vertexCount;
                params.worldExtent = terrainCmp.worldExtent;
                params.heightScale = terrainCmp.heightScale;
                params.heightOffset = terrainCmp.heightOffset;
                params.sourceAabbMin = sourceMesh->aabbMin;
                params.sourceAabbExtent = sourceMesh->aabbExtent;
                params.outputAabbMin = overrideMesh.aabbMin;
                params.outputAabbExtent = overrideMesh.aabbExtent;
                // Node instances carry an identity Transform.scale now (the node's world footprint
                // is baked into the vertex positions/AABB instead -- see Terrain.h
                // TryAttachOverride), so the scale the bake shader needs for the heightmap UV
                // lookup and to bake into the output position is recovered from the override's own
                // AABB (set to source-AABB * scaleXZ at attach time) rather than tr.scale.
                float sourceExtentX = (float)sourceMesh->aabbExtent.x;
                float nodeScaleXZ = sourceExtentX > 1e-8f ? (float)overrideMesh.aabbExtent.x / sourceExtentX : 1.0f;
                params.nodeWorldPosScaleXZ = float4(tr.position, nodeScaleXZ);
                params.heightmapBlurRadius = terrainCmp.heightmapBlurRadius;

                MeshStorage::instance->vertices.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                commandBuffer->SetCompute(*bake);
                commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
                commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
                commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&params));
                commandBuffer->cmd->Dispatch(bake->DispatchX(sourceMesh->vertexCount), 1, 1);
                MeshStorage::instance->vertices.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

                overrideCmp.dirty = 0;
                justBaked.push_back(&overrideMesh);
            }
        }

        // Same-frame BLAS rebuild for everything just baked, IN PLACE (stable VA -- see
        // BuildBLAS's allowInPlaceRebuild): graphics-queue submission order alone (this pass ->
        // culling -> AccelerationStructure's TLAS build, compute queue, fenced behind culling)
        // guarantees the TLAS built this frame already reflects the fresh geometry. Shares
        // MeshStorage's scratchBLAS with that TLAS build (also starting at offset 0) -- safe for
        // the same reason, the fences serialize them.
        if (!justBaked.empty())
        {
            MeshStorage::instance->vertices.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            MeshStorage::instance->indices.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            UINT64 scratchOffset = 0;
            for (Mesh* m : justBaked)
            {
                scratchOffset += MeshStorage::instance->BuildBLAS(*m, 0, m->BLAS, scratchOffset, commandBuffer.Get(), nullptr, nullptr, DXGI_FORMAT_R32_UINT, true);
                if (m->lodCount > 1) // terrain grids are single-LOD today, but stay correct if that changes
                    scratchOffset += MeshStorage::instance->BuildBLAS(*m, m->lodCount - 1, m->BLASLow, scratchOffset, commandBuffer.Get(), nullptr, nullptr, DXGI_FORMAT_R32_UINT, true);
            }
            MeshStorage::instance->vertices.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
            MeshStorage::instance->indices.Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        }

        Close();
    }
};

class AtmosphericScattering : public Pass
{
    ViewResource froxelsBuffer;
    ViewResource atmosphericScatteringFroxels;
    ViewResource atmosphericScatteringHistoryFroxels;
    ViewResource depth;
    Components::Handle<Components::Shader> atmosphericScatteringShader;
    Components::Handle<Components::Shader> atmosphericScatteringReprojectionShader;
    Components::Handle<Components::Shader> atmosphericScatteringBlurShader;
    Components::Handle<Components::Shader> atmosphericScatteringAccumulationShader;

    uint3 atmoSize = float3(160, 90, 256);

public:
    HLSL::AtmosphericScatteringParameters asparams;

    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        froxelsBuffer.Register("froxelsBuffer", view);
        froxelsBuffer.Get().CreateBuffer<HLSL::Froxels>(2, "froxelsBuffer");
        atmosphericScatteringFroxels.Register("atmosphericScatteringFroxels", view);
        atmosphericScatteringFroxels.Get().CreateTexture(atmoSize, DXGI_FORMAT_R16G16B16A16_FLOAT, false, "atmosphericScatteringFroxels");
        atmosphericScatteringHistoryFroxels.Register("atmosphericScatteringHistoryFroxels", view);
        atmosphericScatteringHistoryFroxels.Get().CreateTexture(atmoSize, DXGI_FORMAT_R16G16B16A16_FLOAT, false, "atmosphericScatteringHistoryFroxels");
        depth.Register("depth", view);
        atmosphericScatteringShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\AtmosphericScattering.hlsl|Update");
        atmosphericScatteringReprojectionShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\AtmosphericScattering.hlsl|Reprojection");
        atmosphericScatteringBlurShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\AtmosphericScattering.hlsl|Blur");
        atmosphericScatteringAccumulationShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\AtmosphericScattering.hlsl|Accumulation");

        asparams.density = 0.001;
        asparams.luminosity = 0.1;
        asparams.specialNear = 0.25;
        asparams.heightFalloff = 0.02;
        asparams.noiseFrequency = 0.2;
        asparams.noiseThresholdLow = 0.5;
        asparams.noiseThresholdHigh = 0.6;
        asparams.animationSpeed = 0.00000002;

        Open();
        HLSL::Froxels froxelsData[2];
        froxelsData[0].resolution[0] = (uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Width;
        froxelsData[0].resolution[1] = (uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Height;
        froxelsData[0].resolution[2] = atmosphericScatteringFroxels.Get().GetResource()->GetDesc().DepthOrArraySize;
        froxelsData[0].index = atmosphericScatteringFroxels.Get().uav.offset;
        froxelsData[0].srvIndex = atmosphericScatteringFroxels.Get().srv.offset;
        froxelsData[1].resolution[0] = (uint)atmosphericScatteringHistoryFroxels.Get().GetResource()->GetDesc().Width;
        froxelsData[1].resolution[1] = (uint)atmosphericScatteringHistoryFroxels.Get().GetResource()->GetDesc().Height;
        froxelsData[1].resolution[2] = atmosphericScatteringHistoryFroxels.Get().GetResource()->GetDesc().DepthOrArraySize;
        froxelsData[1].index = atmosphericScatteringHistoryFroxels.Get().uav.offset;
        froxelsData[1].srvIndex = atmosphericScatteringHistoryFroxels.Get().srv.offset;
        froxelsBuffer.Get().UploadElements(froxelsData, ARRAYSIZE(froxelsData), 0, commandBuffer.Get());
        froxelsBuffer.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        Close();

        ExecuteNow();
    }
    void Off() override
    {
        Pass::Off();
        ZoneScoped;
        froxelsBuffer.Unregister();
        atmosphericScatteringFroxels.Unregister();
        atmosphericScatteringHistoryFroxels.Unregister();
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        // Trace atmospheric scattering froxels
        Shader& atmosphericScattering = *AssetLibrary::instance->Get<Shader>(atmosphericScatteringShader.Get().id, true);
        commandBuffer->SetRaytracing(atmosphericScattering);

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);

        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);

        auto raytracingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, raytracingContextAddress);

        uint inputIndex = (GPU::instance->frameIndex + 0) % 2;
        uint outputIndex = (GPU::instance->frameIndex + 1) % 2;
        asparams.froxelsIndex = froxelsBuffer.Get().srv.offset; // read-only StructuredBuffer<HLSL::Froxels> in every shader that binds this (AtmosphericScattering.hlsl, postprocessHalfRes.hlsl)
        asparams.currentFroxelIndex = 0;
        asparams.historyFroxelIndex = 1;

        auto atmosphericScatteringAddress = ConstantBuffer::instance->PushConstantBuffer(&asparams);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom2Register, atmosphericScatteringAddress);

        D3D12_DISPATCH_RAYS_DESC drd = atmosphericScattering.GetRTDesc();
        drd.Width = (uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Width;
        drd.Height = (uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Height;
        drd.Depth = atmosphericScatteringFroxels.Get().GetResource()->GetDesc().DepthOrArraySize;

        // Both froxel volumes are created (and left, at the end of this function / PostProcessHalfRes)
        // in COMMON -- textures, unlike buffers, are never implicitly promoted into a UAV-writable
        // state, so every compute pass touching them needs an explicit transition first. .Barrier()
        // below is only a UAV hazard barrier (ordering read-after-write for a resource already in
        // UAV state) -- it does not perform this transition.
        atmosphericScatteringFroxels.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // history: read-only (SRV) this frame in Reprojection below (last frame's blurred/accumulated
        // result) before Blur/Accumulation overwrite it via UAV.
        atmosphericScatteringHistoryFroxels.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        commandBuffer->cmd->DispatchRays(&drd);

        atmosphericScatteringFroxels.Get().Barrier(commandBuffer.Get());


        // Reproject atmospheric scattering froxels
        Shader& atmosphericScatteringReprojection = *AssetLibrary::instance->Get<Shader>(atmosphericScatteringReprojectionShader.Get().id, true);
        commandBuffer->SetCompute(atmosphericScatteringReprojection);
        commandBuffer->cmd->Dispatch(
            atmosphericScatteringReprojection.DispatchX((uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Width),
            atmosphericScatteringReprojection.DispatchY((uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Height),
            atmosphericScatteringReprojection.DispatchY(atmosphericScatteringFroxels.Get().GetResource()->GetDesc().DepthOrArraySize));

        atmosphericScatteringFroxels.Get().Barrier(commandBuffer.Get());
        // history goes from SRV (Reprojection's read, just above) to UAV (Blur's write, just below)
        atmosphericScatteringHistoryFroxels.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


        // Blur atmospheric scattering froxels (XY box filter, ping-pong into history buffer)
        Shader& atmosphericScatteringBlur = *AssetLibrary::instance->Get<Shader>(atmosphericScatteringBlurShader.Get().id, true);
        commandBuffer->SetCompute(atmosphericScatteringBlur);
        commandBuffer->cmd->Dispatch(
            atmosphericScatteringBlur.DispatchX((uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Width),
            atmosphericScatteringBlur.DispatchY((uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Height),
            atmosphericScatteringBlur.DispatchY(atmosphericScatteringFroxels.Get().GetResource()->GetDesc().DepthOrArraySize));

        atmosphericScatteringHistoryFroxels.Get().Barrier(commandBuffer.Get());


        // Accumulate atmospheric scattering froxels
        Shader& atmosphericScatteringAccumulation = *AssetLibrary::instance->Get<Shader>(atmosphericScatteringAccumulationShader.Get().id, true);
        commandBuffer->SetCompute(atmosphericScatteringAccumulation);
        commandBuffer->cmd->Dispatch(
            atmosphericScatteringAccumulation.DispatchX((uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Width),
            atmosphericScatteringAccumulation.DispatchY((uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Height),
            1);

        // Back to COMMON on both: current is read as SRV by PostProcessHalfRes next (its own
        // transition brackets that), history goes back to SRV at the top of next frame's Reprojection.
        atmosphericScatteringFroxels.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        atmosphericScatteringHistoryFroxels.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

        Close();
    }
};

class PostProcessHalfRes : public Pass
{
    ViewResource lighted;
    ViewResource albedo;
    ViewResource normal;
    ViewResource motion;
    ViewResource depth;
    ViewResource transparencyLayer;
    ViewResource atmosphericScatteringFroxelsBuffer;
    ViewResource atmosphericScatteringFroxels;
    Components::Handle<Components::Shader> postProcessHalfResShader;

public:
    HLSL::PostProcessHalfResParameters pphrparams;
    // Copy of the blended AtmosphericScattering params, written by MainView::UpdateRenderSettings
    // (MainView is not declared yet at this point in the header). The shader samples the froxel
    // volume and must use the same specialNear the volume was built with.
    HLSL::AtmosphericScatteringParameters asparams = {};

    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        asparams.specialNear = 0.25f; // must match AtmosphericScattering::On until the first UpdateRenderSettings overwrites it
        transparencyLayer.Register("transparencyLayer", view);
        transparencyLayer.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R16G16B16A16_FLOAT, "transparencyLayer"); // must be the same as Lighted input
        lighted.Register("lighted", view);
        albedo.Register("albedo", view);
        normal.Register("normal", view);
        motion.Register("motion", view);
        depth.Register("depth", view);
        atmosphericScatteringFroxelsBuffer.Register("froxelsBuffer", view);
        atmosphericScatteringFroxels.Register("atmosphericScatteringFroxels", view);
        postProcessHalfResShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\PostProcessHalfRes.hlsl|PostProcessHalfRes");

        // Read+written via RWTexture2D every frame here and nowhere else needs it in any other
        // state (DLSS-RR reads it raw downstream) -- one-time transition, no per-frame bracket needed.
        Open();
        transparencyLayer.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Close();
        ExecuteNow();
    }
    void Off() override
    {
        Pass::Off();
        ZoneScoped;
        transparencyLayer.Unregister();
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);

        pphrparams.froxelsIndex = atmosphericScatteringFroxelsBuffer.Get().srv.offset; // read-only StructuredBuffer<HLSL::Froxels> in postprocessHalfRes.hlsl
        pphrparams.atmosphericScatteringIndex = 0;
        pphrparams.lightedIndex = lighted.Get().uav.offset;
        pphrparams.transparencyLayerIndex = transparencyLayer.Get().uav.offset;

        Shader& postProcessHalfRes = *AssetLibrary::instance->Get<Shader>(postProcessHalfResShader.Get().id, true);
        commandBuffer->SetCompute(postProcessHalfRes);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&pphrparams));
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom2Register, ConstantBuffer::instance->PushConstantBuffer(&asparams));

        // AtmosphericScattering::Render leaves this in COMMON (see its own end-of-frame transition);
        // sampled here via Texture3D<>.Sample(), which needs an explicit SRV-compatible state.
        atmosphericScatteringFroxels.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // Lighting::Render leaves depth in DEPTH_WRITE at the end of its frame (its own end-of-frame
        // transition) for next frame's zPrepass -- but common.hlsl's GetGBufferCameraData (called
        // here via viewContext.depthIndex) reads it as a plain Texture2D SRV, which for a
        // DEPTH_STENCIL-flagged resource needs the DEPTH_READ state combined with the shader-visible
        // read state, not DEPTH_WRITE.
        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commandBuffer->cmd->Dispatch(postProcessHalfRes.DispatchX(view->renderResolution.x), postProcessHalfRes.DispatchY(view->renderResolution.y), 1);
        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        atmosphericScatteringFroxels.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);

        Close();
    }
};

class PostProcess : public Pass
{
    ViewResource postProcessed;
    ViewResource lighted;
    ViewResource albedo;
    ViewResource normal;
    ViewResource motion;
    ViewResource depth;
    ViewResource history;
    ViewResource upscaled;
    Components::Handle<Components::Shader> postProcessShader;
    Components::Handle<Components::Shader> TAAShader;

public:
    HLSL::PostProcessParameters ppparams;
    HLSL::TAAParameters taaparams;

    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        postProcessed.Register("postProcessed", view);
        postProcessed.Get().CreateRenderTarget(view->displayResolution, DXGI_FORMAT_R8G8B8A8_UNORM, "postProcessed"); // must be same as backbuffer for a resource copy at end of frame 
        history.Register("history", view);
        history.Get().CreateTexture(view->renderResolution, DXGI_FORMAT_R16G16B16A16_FLOAT, false, "history"); // must be the same as Lighted input
        lighted.Register("lighted", view);
        albedo.Register("albedo", view);
        normal.Register("normal", view);
        motion.Register("motion", view);
        depth.Register("depth", view);
        upscaled.Register("upscaled", view);
        postProcessShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\PostProcess.hlsl|PostProcess");
        TAAShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\TAA.hlsl|TAA");

        ppparams.P = 1;
        ppparams.a = 0.33;
        ppparams.m = 0.22;
        ppparams.l = 0.4;
        ppparams.c = 1.33;
        ppparams.b = 0.0;
        ppparams.expoAdd = 0;
        ppparams.expoMul = 1;


        Open();
        postProcessed.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandBuffer.Get().cmd->DiscardResource(postProcessed.Get().GetResource(), nullptr);
        postProcessed.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Close();
        ExecuteNow();
    }
    void Off() override
    {
        Pass::Off();
        ZoneScoped;
        postProcessed.Unregister();
        history.Unregister();
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);

        if (view->upscaling == HLSL::Upscaling::taa)
        {
            taaparams.lightedIndex = lighted.Get().uav.offset;
            taaparams.historyIndex = history.Get().srv.offset; // read-only Texture2D<>.SampleLevel() in TAA.hlsl, not RW

            // history sits at rest in COMMON; TAA.hlsl samples it as SRV, which -- unlike the implicit
            // promotion a read from COMMON would normally get -- we make explicit here so the COPY_DEST
            // transition below has an unambiguous, verified "before" state instead of relying on
            // mid-command-list implicit-promotion decay timing.
            history.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            Shader& TAA = *AssetLibrary::instance->Get<Shader>(TAAShader.Get().id, true);
            commandBuffer->SetCompute(TAA);
            commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&taaparams));
            commandBuffer->cmd->Dispatch(TAA.DispatchX(view->renderResolution.x), TAA.DispatchY(view->renderResolution.y), 1);

            history.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            // lighted lives permanently in UNORDERED_ACCESS (see Lighting::On) -- CopyResource's
            // source needs COPY_SOURCE instead, restored right after for whatever reads it next.
            lighted.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            commandBuffer->cmd->CopyResource(history.Get().GetResource(), lighted.Get().GetResource());
            lighted.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            history.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON); // transition to present in the editor cmb
        }

        if (view->upscaling == HLSL::Upscaling::dlss || view->upscaling == HLSL::Upscaling::dlssd)
        {
            ppparams.inputIsFullResolution = 1;
            ppparams.lightedIndex = upscaled.Get().uav.offset;
        }
        else
        {
            ppparams.inputIsFullResolution = 0;
            ppparams.lightedIndex = lighted.Get().uav.offset;
        }
        ppparams.postProcessedIndex = postProcessed.Get().uav.offset;
        ppparams.backBufferIndex = GPU::instance->backBuffer.Get().uav.offset;

        Shader& postProcess = *AssetLibrary::instance->Get<Shader>(postProcessShader.Get().id, true);
        commandBuffer->SetCompute(postProcess);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&ppparams));
        commandBuffer->cmd->Dispatch(postProcess.DispatchX(view->displayResolution.x), postProcess.DispatchY(view->displayResolution.y), 1);

        Close();
    }
};

// https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_helpers_dlssd.h"
class DLSS : public Pass
{
    ViewResource albedo;
    ViewResource specularAlbedo;
    ViewResource normal;
    ViewResource motion;
    ViewResource depth;
    ViewResource lighted;
    ViewResource transparencyLayer;
    ViewResource specularHitDistance;
    ViewResource upscaled;

public:
    NVSDK_NGX_Parameter* ngx_parameters = nullptr;
    NVSDK_NGX_Handle* dlss_feature = nullptr;
    NVSDK_NGX_PerfQuality_Value perf_quality = NVSDK_NGX_PerfQuality_Value_Balanced;// NVSDK_NGX_PerfQuality_Value_MaxQuality;// NVSDK_NGX_PerfQuality_Value_MaxPerf;
    float sharpness = 0.0033f;
    bool initialized = false;
    bool created = false;
    HLSL::Upscaling upscalingPreviousSetting;

    NVSDK_NGX_PerfQuality_Value requestedQuality = NVSDK_NGX_PerfQuality_Value_Balanced;
    bool qualityChangePending = false;

    NVSDK_NGX_DLSS_Hint_Render_Preset dlssPreset = NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset dlssdPreset = NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default;
    bool featureDirty = false;

    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        albedo.Register("albedo", view);
        specularAlbedo.Register("specularAlbedo", view);
        normal.Register("normal", view);
        motion.Register("motion", view);
        depth.Register("depth", view);
        lighted.Register("lighted", view);
        transparencyLayer.Register("transparencyLayer", view);
        specularHitDistance.Register("specularHitDistance", view);
        upscaled.Register("upscaled", view);
        upscaled.Get().CreateRenderTarget(view->displayResolution, DXGI_FORMAT_R16G16B16A16_FLOAT, "upscaled"); // must be the same as Lighted input

        // NGX Evaluate writes this as its output resource (raw pointer, DLSS/DLSS-RR convention:
        // UNORDERED_ACCESS); PostProcess::Render reads it back via UAV too (ppparams.lightedIndex =
        // upscaled.Get().uav.offset) -- nothing else needs it in any other state.
        Open();
        upscaled.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Close();
        ExecuteNow();
    }
    virtual void Off() override
    {
        Pass::Off();
        ZoneScoped;
        upscaled.Unregister();
        NVSDK_NGX_D3D12_Shutdown1(GPU::instance->device);
    }
    void Setup(View* view) override
    {
        ZoneScoped;

    }

    void ApplyPresetHint(View* view)
    {
        if (!ngx_parameters)
            return;

        if (view->upscaling == HLSL::Upscaling::dlss)
        {
            const char* slot = nullptr;
            switch (perf_quality)
            {
            case NVSDK_NGX_PerfQuality_Value_MaxPerf:          slot = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance; break;
            case NVSDK_NGX_PerfQuality_Value_Balanced:         slot = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced; break;
            case NVSDK_NGX_PerfQuality_Value_MaxQuality:       slot = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality; break;
            case NVSDK_NGX_PerfQuality_Value_UltraPerformance: slot = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance; break;
            case NVSDK_NGX_PerfQuality_Value_UltraQuality:     slot = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality; break;
            case NVSDK_NGX_PerfQuality_Value_DLAA:             slot = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA; break;
            }
            if (slot)
                ngx_parameters->Set(slot, (unsigned int)dlssPreset);
        }
        else if (view->upscaling == HLSL::Upscaling::dlssd)
        {
            const char* slot = nullptr;
            switch (perf_quality)
            {
            case NVSDK_NGX_PerfQuality_Value_MaxPerf:          slot = NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance; break;
            case NVSDK_NGX_PerfQuality_Value_Balanced:         slot = NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced; break;
            case NVSDK_NGX_PerfQuality_Value_MaxQuality:       slot = NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality; break;
            case NVSDK_NGX_PerfQuality_Value_UltraPerformance: slot = NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance; break;
            case NVSDK_NGX_PerfQuality_Value_UltraQuality:     slot = NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality; break;
            case NVSDK_NGX_PerfQuality_Value_DLAA:             slot = NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA; break;
            }
            if (slot)
                ngx_parameters->Set(slot, (unsigned int)dlssdPreset);
        }
    }

    void CreateDLSS(View* view, uint2 displayResolution, uint2& renderResolution)
    {
        if (initialized)
            return;

        ZoneScoped;
        view->upscaling = HLSL::Upscaling::taa;
        //return;

        // cant find _nvngx.dll or nvmgx.dll ... copied from some driver repo in the OS (do a global search)
        // in faact its not needed it is working fine on the laptop .... why not on the descktop ?
        // why does it work on the laptop 4050 ?!

        // Turns out we can simply ignore the exception ...

        static const wchar_t* dll_paths[] =
        {
            L".",
        };

        NVSDK_NGX_FeatureCommonInfo feature_common_info{};
        feature_common_info.LoggingInfo.LoggingCallback = [](const char* msg, NVSDK_NGX_Logging_Level level, NVSDK_NGX_Feature source) {
            IOs::Log("{}", msg);
            };
#ifdef _DEBUG
        feature_common_info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;
#else
        feature_common_info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;
#endif
        feature_common_info.LoggingInfo.DisableOtherLoggingSinks = true;
        feature_common_info.PathListInfo.Path = dll_paths;
        feature_common_info.PathListInfo.Length = NVSDK_NGX_ARRAY_LEN(dll_paths);

        NVSDK_NGX_Application_Identifier ngx_app_id = {};
        ngx_app_id.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
        ngx_app_id.v.ApplicationId = 0xdeadbeef;

        NVSDK_NGX_FeatureDiscoveryInfo featureDiscoveryInfo = {};
        featureDiscoveryInfo.SDKVersion = NVSDK_NGX_Version_API;
        featureDiscoveryInfo.FeatureID = NVSDK_NGX_Feature_RayReconstruction;
        featureDiscoveryInfo.Identifier = ngx_app_id;
        featureDiscoveryInfo.ApplicationDataPath = std::filesystem::temp_directory_path().wstring().c_str();
        featureDiscoveryInfo.FeatureInfo = &feature_common_info;

        NVSDK_NGX_FeatureRequirement dlssdSupported = {};
        NVSDK_NGX_Result featureReq = NVSDK_NGX_D3D12_GetFeatureRequirements(GPU::instance->adapter, &featureDiscoveryInfo, &dlssdSupported);

        NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init(
            ngx_app_id.v.ApplicationId,
            featureDiscoveryInfo.ApplicationDataPath,
            GPU::instance->device,
            &feature_common_info);
        if (NVSDK_NGX_FAILED(result)) return;

        result = NVSDK_NGX_D3D12_GetCapabilityParameters(&ngx_parameters);
        if (NVSDK_NGX_FAILED(result)) return;

        int needs_updated_driver = 0;
        uint min_driver_version_major = 0;
        uint min_driver_version_minor = 0;
        NVSDK_NGX_Result result_updated_driver = ngx_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_updated_driver);
        NVSDK_NGX_Result result_min_driver_version_major = ngx_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &min_driver_version_major);
        NVSDK_NGX_Result result_min_driver_version_minor = ngx_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &min_driver_version_minor);
        if (NVSDK_NGX_SUCCEED(result_updated_driver))
        {
            if (needs_updated_driver)
            {
                if (NVSDK_NGX_SUCCEED(result_min_driver_version_major) &&
                    NVSDK_NGX_SUCCEED(result_min_driver_version_minor))
                {
                    IOs::Log("Nvidia DLSS cannot be loaded due to outdated driver, min driver version: %ul.%ul", min_driver_version_major, min_driver_version_minor);
                    return;
                }
                IOs::Log("Nvidia DLSS cannot be loaded due to outdated driver");
            }
        }

        int dlss_available = 0;
        result = ngx_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlss_available);
        if (NVSDK_NGX_FAILED(result) || !dlss_available) return;

        uint renderWidth, renderHeight;
        uint renderMaxWidth, renderMaxHeight;
        uint renderMinWidth, renderMinHeight;
        result = NGX_DLSS_GET_OPTIMAL_SETTINGS(ngx_parameters, displayResolution.x, displayResolution.y, perf_quality, &renderWidth, &renderHeight, &renderMaxWidth, &renderMaxHeight, &renderMinWidth, &renderMinHeight, &sharpness);

        if (NVSDK_NGX_FAILED(result)) 
            return;

        renderResolution.x = renderWidth;
        renderResolution.y = renderHeight;

        initialized = true;
        created = false;
        view->upscaling = HLSL::Upscaling::dlss;
        if(dlssdSupported.FeatureSupported == NVSDK_NGX_FeatureSupportResult_Supported)
            view->upscaling = HLSL::Upscaling::dlssd;

        upscalingPreviousSetting = HLSL::Upscaling::none;
    }

    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        if (view->upscaling == HLSL::Upscaling::dlss || view->upscaling == HLSL::Upscaling::dlssd)
        {
            lighted.Get().Barrier(commandBuffer.Get());

            // NGX Evaluate reads every input resource by raw ID3D12Resource*, no descriptor involved,
            // but D3D12 barrier tracking still applies. Per NVIDIA's DLSS/DLSS-RR programming guide,
            // color/vector inputs and the output are expected in UNORDERED_ACCESS; depth is the
            // exception (depth-stencil-format resources can't even have a UAV -- CreateDepthTarget
            // never creates one -- so it goes in via the same SRV-combo read state used elsewhere in
            // this file). lighted/transparencyLayer/specularHitDistance/upscaled already live in
            // UNORDERED_ACCESS at rest (see their one-time init transitions), so only the resources
            // that sit in COMMON between GBuffers and here need a bracket.
            depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (view->upscaling == HLSL::Upscaling::dlssd)
            {
                normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }

            if (upscalingPreviousSetting != view->upscaling || featureDirty)
            {
                if (dlss_feature)
                {
                    NVSDK_NGX_D3D12_ReleaseFeature(dlss_feature);
                    dlss_feature = nullptr;
                }

                ApplyPresetHint(view); // must precede the create call below

                if (view->upscaling == HLSL::Upscaling::dlss)
                {
                    NVSDK_NGX_DLSS_Create_Params dlss_create_params{};
                    dlss_create_params.Feature.InWidth = view->renderResolution.x;
                    dlss_create_params.Feature.InHeight = view->renderResolution.y;
                    dlss_create_params.Feature.InTargetWidth = view->displayResolution.x;
                    dlss_create_params.Feature.InTargetHeight = view->displayResolution.y;
                    dlss_create_params.Feature.InPerfQualityValue = perf_quality;
                    dlss_create_params.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                        NVSDK_NGX_DLSS_Feature_Flags_MVJittered |
                        //NVSDK_NGX_DLSS_Feature_Flags_AutoExposure |
                        //NVSDK_NGX_DLSS_Feature_Flags_DoSharpening |
                        NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
                    dlss_create_params.InEnableOutputSubrects = false;

                    NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(commandBuffer->cmd, 0, 0, &dlss_feature, ngx_parameters, &dlss_create_params);
                    seedAssert(NVSDK_NGX_SUCCEED(result));
                }
                else if (view->upscaling == HLSL::Upscaling::dlssd)
                {
                    NVSDK_NGX_DLSSD_Create_Params dlssd_create_params{};
                    dlssd_create_params.InWidth = view->renderResolution.x;
                    dlssd_create_params.InHeight = view->renderResolution.y;
                    dlssd_create_params.InTargetWidth = view->displayResolution.x;
                    dlssd_create_params.InTargetHeight = view->displayResolution.y;
                    dlssd_create_params.InPerfQualityValue = perf_quality;
                    dlssd_create_params.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode::NVSDK_NGX_DLSS_Roughness_Mode_Packed; // roughness lives in normals.w (one less gbuffer target)
                    dlssd_create_params.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type::NVSDK_NGX_DLSS_Depth_Type_HW;
                    dlssd_create_params.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode::NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
                    dlssd_create_params.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                        //NVSDK_NGX_DLSS_Feature_Flags_MVJittered |
                        //NVSDK_NGX_DLSS_Feature_Flags_AutoExposure |
                        //NVSDK_NGX_DLSS_Feature_Flags_DoSharpening |
                        NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

                    NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSSD_EXT(commandBuffer->cmd, 0, 0, &dlss_feature, ngx_parameters, &dlssd_create_params);
                    seedAssert(NVSDK_NGX_SUCCEED(result));
                }

                created = true;
                featureDirty = false;
                upscalingPreviousSetting = view->upscaling;
            }

            if (view->upscaling == HLSL::Upscaling::dlss)
            {
                NVSDK_NGX_D3D12_DLSS_Eval_Params dlss_eval_params{};
                dlss_eval_params.Feature.pInColor = lighted.Get().GetResource();
                dlss_eval_params.Feature.pInOutput = upscaled.Get().GetResource();
                dlss_eval_params.pInDepth = depth.Get().GetResource();
                dlss_eval_params.pInMotionVectors = motion.Get().GetResource();
                dlss_eval_params.InMVScaleX = (float)view->renderResolution.x; // because my MotionVectors are in uv space
                dlss_eval_params.InMVScaleY = (float)view->renderResolution.y;

                dlss_eval_params.Feature.InSharpness = sharpness;

                dlss_eval_params.pInExposureTexture = nullptr;
                dlss_eval_params.InExposureScale = 1.0f;

                // Jitter offset must ALWAYS be supplied (independent of MVJittered). Our projection
                // applies an NDC offset of (halton-0.5)/renderResolution, so the pixel-space offset
                // DLSS expects is (halton-0.5)*0.5.
                float2 dlssJitter = view->viewContext.jitter[view->viewContext.jitterIndex];
                dlss_eval_params.InJitterOffsetX = (dlssJitter.x - 0.5f) * 0.5f;
                dlss_eval_params.InJitterOffsetY = (dlssJitter.y - 0.5f) * 0.5f;

                dlss_eval_params.InReset = false;
                dlss_eval_params.InRenderSubrectDimensions = { view->renderResolution.x, view->renderResolution.y };

                NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSS_EXT(commandBuffer->cmd, dlss_feature, ngx_parameters, &dlss_eval_params);
                seedAssert(NVSDK_NGX_SUCCEED(result));
            }
            else if (view->upscaling == HLSL::Upscaling::dlssd)
            {
                NVSDK_NGX_D3D12_DLSSD_Eval_Params dlss_eval_params{};
                dlss_eval_params.InReset = 0;
                dlss_eval_params.InRenderSubrectDimensions = { view->renderResolution.x, view->renderResolution.y };
                dlss_eval_params.pInColor = lighted.Get().GetResource();
                dlss_eval_params.pInColorAfterFog = lighted.Get().GetResource();
                dlss_eval_params.pInOutput = upscaled.Get().GetResource();
                dlss_eval_params.pInNormals = normal.Get().GetResource(); // .w = roughness (packed mode)
                dlss_eval_params.pInDiffuseAlbedo = albedo.Get().GetResource();
                dlss_eval_params.pInSpecularAlbedo = specularAlbedo.Get().GetResource();
                dlss_eval_params.pInDepth = depth.Get().GetResource();
                dlss_eval_params.pInMotionVectors = motion.Get().GetResource();
                dlss_eval_params.pInSpecularHitDistance = specularHitDistance.Get().GetResource();
                dlss_eval_params.pInTransparencyLayer = transparencyLayer.Get().GetResource();
                dlss_eval_params.InFrameTimeDeltaInMsec = Time::instance->deltaSeconds * 1000.0f;
                dlss_eval_params.InMVScaleX = (float)view->renderResolution.x; // because my MotionVectors are in uv space
                dlss_eval_params.InMVScaleY = (float)view->renderResolution.y;
                dlss_eval_params.pInWorldToViewMatrix = reinterpret_cast<float*>(&view->viewWorld.cameras.Get()[0].view);
                dlss_eval_params.pInViewToClipMatrix = reinterpret_cast<float*>(&view->viewWorld.cameras.Get()[0].proj);

                //dlss_eval_params.pInExposureTexture = nullptr; // not supported
                dlss_eval_params.InExposureScale = 1.0f;
                //dlss_eval_params.pInAlpha = albedo.Get().GetResource();

                // Jitter offset must ALWAYS be supplied (independent of MVJittered, which only
                // describes whether the MVs already contain jitter). Our projection applies an
                // NDC offset of (halton-0.5)/renderResolution (see mesh.hlsl), so the pixel-space
                // offset DLSS expects is (halton-0.5)*0.5.
                float2 dlssdJitter = view->viewContext.jitter[view->viewContext.jitterIndex];
                dlss_eval_params.InJitterOffsetX = (dlssdJitter.x - 0.5f) * 0.5f;
                dlss_eval_params.InJitterOffsetY = (dlssdJitter.y - 0.5f) * 0.5f;

                NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSSD_EXT(commandBuffer->cmd, dlss_feature, ngx_parameters, &dlss_eval_params);
                seedAssert(NVSDK_NGX_SUCCEED(result));

                normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
                albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
                specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            }

            motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }

        Close();
    }
};

class Present : public Pass
{
    ViewResource postProcessed;
    ViewResource depth;
public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        postProcessed.Register("postProcessed", view);
        depth.Register("depth", view);
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();
        GPU::instance->backBuffer->Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        postProcessed.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandBuffer->cmd->CopyResource(GPU::instance->backBuffer.Get().GetResource(), postProcessed.Get().GetResource());
        postProcessed.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        GPU::instance->backBuffer->Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET); // transition to present in the editor cmb
        Close();
    }
};


// After recording, mark this pass ready and submit every now-contiguous-ready pass from the cursor.
// Whichever worker unblocks the cursor performs the submission inline -- no dedicated submit thread.
#define SUBTASKVIEWPASS(pass) tf::Task pass##Task = subflow.emplace([this](){this->pass.Render(this); this->submissions->MarkReadyAndDrain(this->pass.submitIndex);}).name(#pass)
#define SUBTASKPASS(pass) tf::Task pass##Task = subflow.emplace([this](){this->pass.Render(nullptr); this->submissions->MarkReadyAndDrain(this->pass.submitIndex);}).name(#pass)
#define SUBTASKRENDERERWORLD(pass) tf::Task pass = subflow.emplace([this, world](){this->pass(world);}).name(#pass)
#define SUBTASKRENDERER(pass) tf::Task pass = subflow.emplace([this](){this->pass();}).name(#pass)

// a view per type of render ? one for main view, one for cubemap, one for minimap ?
// main view render should always be the last one to render ?\

class MainView : public View
{
public:
    std::mutex InstanceRTSync;
    GPUDebugInit gpuDebugInit;
    StructuredCommandBufferUpdate structuredCommandBufferUpdate;
    HZB hzb;
    Skinning skinning;
    Particles particles;
    Spawning spawning;
    TerrainErosion terrain;
    Culling culling;
    ZPrepass zPrepass;
    AccelerationStructure accelerationStructure;
    AtmosphericScattering atmospehricScattering;
    GBuffers gBuffers;
    LightingProbes lightingProbes;
    Lighting lighting;
    Forward forward;
    PostProcessHalfRes postProcessHalfRes;
    GPUDebug gpuDebug;
    DLSS dlss;
    PostProcess postProcess;
    Present present;


    Components::RenderSettingsVolume baseSettings;
    bool baseSettingsCaptured = false;

    HLSL::Camera mainCamera = {}; // last-computed main (camera index 0) camera; read directly by Systems::TerrainStreaming via Renderer::instance->mainView

    void On(uint2 _displayResolution, uint2 _renderResolution) override
    {
        ZoneScoped;

        dlss.CreateDLSS(this, _displayResolution, _renderResolution);

        View::On(_displayResolution, _renderResolution);

        hzb.On(this, GPU::instance->graphicQueue, "hzb", nullptr, nullptr);
        structuredCommandBufferUpdate.On(this, GPU::instance->computeQueue, "structuredCommandBufferUpdate", nullptr, nullptr);
        gpuDebugInit.On(this, GPU::instance->graphicQueue, "gpuDebugInit", &hzb.commandBuffer, nullptr);
        terrain.On(this, GPU::instance->graphicQueue, "terrain", &AssetLibrary::instance->commandBuffer, nullptr);
        skinning.On(this, GPU::instance->computeQueue, "skinning", &AssetLibrary::instance->commandBuffer, &structuredCommandBufferUpdate.commandBuffer);
        particles.On(this, GPU::instance->computeQueue, "particles", &AssetLibrary::instance->commandBuffer, nullptr);
        spawning.On(this, GPU::instance->computeQueue, "spawning", &AssetLibrary::instance->commandBuffer, nullptr);
        culling.On(this, GPU::instance->graphicQueue, "culling", &structuredCommandBufferUpdate.commandBuffer , &hzb.commandBuffer);
        zPrepass.On(this, GPU::instance->graphicQueue, "zPrepass", &culling.commandBuffer, nullptr);
        accelerationStructure.On(this, GPU::instance->computeQueue, "accelerationStructure", &culling.commandBuffer, nullptr);
        atmospehricScattering.On(this, GPU::instance->computeQueue, "atmospehricScattering", &accelerationStructure.commandBuffer, nullptr);
        gBuffers.On(this, GPU::instance->graphicQueue, "gBuffers", &zPrepass.commandBuffer, nullptr);
        lightingProbes.On(this, GPU::instance->computeQueue, "lightingProbes", &gBuffers.commandBuffer, &accelerationStructure.commandBuffer);
        lighting.On(this, GPU::instance->graphicQueue, "lighting", &gBuffers.commandBuffer, &lightingProbes.commandBuffer);
        forward.On(this, GPU::instance->graphicQueue, "forward", &lighting.commandBuffer, &atmospehricScattering.commandBuffer);
        postProcessHalfRes.On(this, GPU::instance->graphicQueue, "postProcessHalfRes", &forward.commandBuffer, nullptr);
        gpuDebug.On(this, GPU::instance->graphicQueue, "gpuDebug", &forward.commandBuffer, nullptr);
        dlss.On(this, GPU::instance->graphicQueue, "dlss", &forward.commandBuffer, nullptr);
        postProcess.On(this, GPU::instance->graphicQueue, "postProcess", &dlss.commandBuffer, nullptr);
        present.On(this, GPU::instance->graphicQueue, "present", &gpuDebug.commandBuffer, nullptr);
    }

    void Off() override
    {
        ZoneScoped;
        gpuDebugInit.Off();
        structuredCommandBufferUpdate.Off();
        hzb.Off();
        skinning.Off();
        particles.Off();
        spawning.Off();
        terrain.Off();
        culling.Off();
        zPrepass.Off();
        accelerationStructure.Off();
        atmospehricScattering.Off();
        gBuffers.Off();
        lightingProbes.Off();
        lighting.Off();
        forward.Off();
        postProcessHalfRes.Off();
        gpuDebug.Off();
        dlss.Off();
        postProcess.Off();
        present.Off();

        View::Off();
    }

    tf::Task Schedule(World& world, tf::Subflow& subflow) override
    {
        ZoneScoped;

        tf::Task reset = Reset(world, subflow);
        tf::Task updateMaterials = UpdateMaterials(world, subflow);
        tf::Task updateInstances = UpdateInstances(world, subflow);
        tf::Task updateLights = UpdateLights(world, subflow);
        tf::Task updateCameras = UpdateCameras(world, subflow);
        tf::Task updateRenderSettings = UpdateRenderSettings(world, subflow);

        tf::Task uploadAndSetup = UploadAndSetup(world, subflow);

        SUBTASKVIEWPASS(gpuDebugInit);
        SUBTASKVIEWPASS(structuredCommandBufferUpdate);
        SUBTASKVIEWPASS(hzb);
        SUBTASKVIEWPASS(skinning);
        SUBTASKVIEWPASS(particles);
        SUBTASKVIEWPASS(spawning);
        SUBTASKVIEWPASS(terrain);
        SUBTASKVIEWPASS(culling);
        SUBTASKVIEWPASS(zPrepass);
        SUBTASKVIEWPASS(accelerationStructure);
        SUBTASKVIEWPASS(atmospehricScattering);
        SUBTASKVIEWPASS(gBuffers);
        SUBTASKVIEWPASS(lightingProbes);
        SUBTASKVIEWPASS(lighting);
        SUBTASKVIEWPASS(forward);
        SUBTASKVIEWPASS(gpuDebug);
        SUBTASKVIEWPASS(postProcessHalfRes);
        SUBTASKVIEWPASS(dlss);
        SUBTASKVIEWPASS(postProcess);
        SUBTASKVIEWPASS(present);

        reset.precede(updateInstances, updateMaterials, updateLights, updateCameras); // should precede all, user need to check that
        updateInstances.succeed(updateMaterials);
        structuredCommandBufferUpdateTask.succeed(updateInstances);
        uploadAndSetup.succeed(structuredCommandBufferUpdateTask, updateLights, updateCameras);
        updateRenderSettings.precede(atmospehricScatteringTask, postProcessHalfResTask, postProcessTask);
        // no need to put unnecessary dependencies on upload and setup (passes that do not use the view world data)
        // weeeelllll for all passes that use the camera need to wait for upload and setup to be sure the camera data is updated (just in case the buffers used are new because they are bigger)
        // gpuDebugInit/gpuDebug push commonResourcesIndices/viewContext/editorContext the same way every
        // other pass here does, but were missing from this list -- with no edge to uploadAndSetup (the
        // only place those CBs get (re)populated for the frame, see UploadAndSetup/SetupEditorParams),
        // Taskflow was free to run them concurrently with (or before) uploadAndSetup's write, a genuine
        // data race on view->editorContext.editorContext etc. GBV caught it as debug.hlsl reading a
        // garbage descriptor heap index out of editorContext.debugVerticesCountHeapIndex.
        uploadAndSetup.precede(skinningTask, terrainTask, accelerationStructureTask, cullingTask, zPrepassTask, gBuffersTask, lightingProbesTask, lightingTask, atmospehricScatteringTask, postProcessHalfResTask, forwardTask, dlssTask, postProcessTask, gpuDebugInitTask, gpuDebugTask);

        presentTask.succeed(uploadAndSetup,
            structuredCommandBufferUpdateTask,
            hzbTask,
            skinningTask,
            particlesTask,
            spawningTask,
            terrainTask,
            accelerationStructureTask,
            cullingTask,
            zPrepassTask,
            gBuffersTask,
            lightingProbesTask,
            lightingTask,
            forwardTask,
            atmospehricScatteringTask,
            postProcessHalfResTask,
            dlssTask,
            postProcessTask,
            gpuDebugInitTask,
            gpuDebugTask);

        return presentTask;
    }


    tf::Task Reset(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            [this]()
            {
                ZoneScoped;
                // should call a view method with all that ?
                //viewWorld.meshletsCount = 0;
                //viewWorld.instances->Clear();
                //raytracingContext.instancesRayTracing->Clear();
                viewWorld.lights->Clear();
                viewWorld.cameras->Clear();
            }
        ).name("Reset");
        return task;
    }

    tf::Task UpdateInstances(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        viewWorld.meshletsCount = 0;

#define UpdateInstancesStepSize 512 
        ViewWorld& frameWorld = viewWorld;
        
        uint instanceQueryIndex = world.Query(Components::Instance::mask | Components::WorldMatrix::mask, 0, true);
        uint entityCount = (uint)world.frameQueries[instanceQueryIndex].size();
        frameWorld.instances.Reserve(entityCount);

        uint materialsCount = world.CountQuery(Components::Material::mask, 0);
        frameWorld.materials.Reserve(materialsCount);
        
        tf::Task task = subflow.for_each_index(uint(0), entityCount, uint(UpdateInstancesStepSize),
            [this, &world, instanceQueryIndex](int i)
            {
                ZoneScopedN("UpdateInstance");

                uint localMeshletCount = 0;
                uint instanceCount = 0;
                uint instanceRayTracingCount = 0;
                for (uint subQuery = 0; subQuery < UpdateInstancesStepSize; subQuery++)
                {
                    auto& queryResult = world.frameQueries[instanceQueryIndex];
                    if ((i + subQuery) > (queryResult.size() - 1)) 
                        break;

                    auto& slot = queryResult[i + subQuery];

                    Components::State& state = slot.Get<Components::State>();
                    bool loaded = state.flags & Components::State::Flags::loaded;
                    bool dirty = state.flags & Components::State::Flags::dirty;

                    // Catches the case dirty can't: this instance itself wasn't touched, but an
                    // ancestor's Transform was (e.g. moving a parent in the editor) -- see
                    // ComputeTransformVersion (World.h). Walking to the root here is O(depth) and
                    // piggybacks on a query this loop already runs every frame, so it's cheap even
                    // though it (unlike `dirty`) can't be short-circuited by the loaded check below.
                    World::Entity ent = World::Entity(slot.Get<Components::Entity>());
                    Components::WorldMatrix& matrixCmp = slot.Get<Components::WorldMatrix>();
                    uint transformVersion = ComputeTransformVersion(ent);
                    bool ancestryMoved = matrixCmp.versionCache != transformVersion;

                    if (loaded && !dirty && !ancestryMoved)
                        continue;

                    Components::Instance& instanceCmp = slot.Get<Components::Instance>();

                    // an Instance can be created (e.g. by the terrain quadtree, or a not-yet-fully
                    // set-up material) before its mesh/material/shader handles are assigned -- skip
                    // it for now rather than dereferencing an invalid entity below.
                    if (!instanceCmp.mesh.IsValid() || !instanceCmp.material.IsValid())
                        continue;
                    Components::Material& materialCmp = instanceCmp.material.Get();
                    if (!materialCmp.shader.IsValid())
                        continue;

                    Components::Mesh& meshCmp = instanceCmp.mesh.Get();
                    Components::Shader& shaderCmp = materialCmp.shader.Get();

                    Mesh* mesh = AssetLibrary::instance->Get<Mesh>(meshCmp.id);
                    if (!mesh)
                        continue;

                    // Terrain node displacement baking (MeshStorage::CreateMeshOverride): an
                    // instance with a MeshOverride component is redirected to a MeshStorage-owned
                    // override Mesh (its own vertex range + AABB + BLAS, sharing the source's
                    // meshlet/index/LOD data) instead of the component's own mesh -- meshIndex
                    // (gbuffer/RT vertex fetch) and the BLAS VAs below must always point at the
                    // SAME mesh, or raster and RT would disagree.
                    Mesh* effectiveMesh = mesh;
                    if (ent.Has<Components::MeshOverride>())
                    {
                        auto& overrideCmp = ent.Get<Components::MeshOverride>();
                        auto overrideIt = MeshStorage::instance->allMeshes.find(overrideCmp.overrideMeshIndex);
                        if (overrideIt != MeshStorage::instance->allMeshes.end())
                            effectiveMesh = &overrideIt->second;
                    }

                    Shader* shader = AssetLibrary::instance->Get<Shader>(shaderCmp.id);
                    if (!shader)
                        continue;

                    // Captured before the flag is set below: true only the frame this instance
                    // actually transitions from not-loaded to loaded (not on a later dirty-refresh
                    // of an already-loaded instance) -- the exact point the plan calls out for
                    // hooking the incremental shader-bucket meshlet-count contribution.
                    bool firstLoad = !loaded;

                    state.flags |= Components::State::Flags::loaded;

                    uint materialIndex = viewWorld.materials.GetIndex(World::Entity(instanceCmp.material));

                    // everything should be loaded to be able to draw the instance

                    float4x4 previousWorldMatrix = matrixCmp.matrix;
                    matrixCmp.matrix = ComputeWorldMatrix(ent);
                    matrixCmp.versionCache = transformVersion;
                    previousWorldMatrix = matrixCmp.matrix;

                    HLSL::Instance instance;
                    instance.meshIndex = effectiveMesh->storageIndex;
                    instance.materialIndex = materialIndex;
                    instance.current = instance.pack(matrixCmp.matrix);
                    instance.previous = instance.pack(previousWorldMatrix);
                    instance.objectID = ent.ToUInt();
                    instance.rtFlags = 0;
                    instance.boundingSphereOverride = instanceCmp.boundingSphereOverride;
                    // the mesh's OMM was baked against one albedo: if the instance uses a
                    // different texture the baked opacity states are wrong, fall back to pure any-hit
                    // (an override mesh never carries OMM -- ommTextureHash stays 0 -- so this is
                    // naturally a no-op for overridden instances)
                    if (effectiveMesh->ommTextureHash != 0
                        && (!materialCmp.textures[0].IsValid() || materialCmp.textures[0].Get().id.hash != effectiveMesh->ommTextureHash))
                        instance.rtFlags |= HLSL::RTInstanceFlagDisableOMMs;
                    instance.rayTracingBLAS = effectiveMesh->BLAS.GetResource()->GetGPUVirtualAddress();
                    // single-LOD meshes have no low BLAS: alias the high one so culling.hlsl can select blindly
                    instance.rayTracingBLASLow = effectiveMesh->BLASLow.GetResource() ? effectiveMesh->BLASLow.GetResource()->GetGPUVirtualAddress() : instance.rayTracingBLAS;

                    viewWorld.instances.AddOrUpdate(ent, instance);
                    localMeshletCount += effectiveMesh->LODs[0].meshletCount;

                    if (firstLoad)
                    {
                        // This instance's meshlets weren't previously counted in any shader bucket
                        // -- record the exact amount so release can subtract precisely this much
                        // back out later (Instance's RemoveCallback -> ShaderBucketRegistry::
                        // ReleaseInstanceContribution, wired in ViewWorld::On()). Not hit again on
                        // later dirty-refresh frames, so in-place mesh/material mutation of an
                        // already-loaded instance is a known, documented limitation (doesn't happen
                        // anywhere in the codebase today).
                        viewWorld.shaderBucketRegistry.AddInstanceContribution(ent, shaderCmp.id, effectiveMesh->LODs[0].meshletCount);
                    }

                    // dirty is consumed exactly here (this is its only reader): clear it so an
                    // instance edited once (gizmo, terrain node pool reuse) is re-uploaded once,
                    // not every frame forever. Cleared only when actually processed -- the
                    // early-continues above (mesh/material/shader not ready) keep the flag so the
                    // pending change isn't lost.
                    state.flags.Unset(Components::State::Flags::dirty);

                    // count instances with shader
                    instanceCount++;

                }

                viewWorld.meshletsCount += localMeshletCount;
            }
        );

        return task;
    }

    tf::Task UpdateMaterials(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        // parallel for materials in frameworld
            // add textures in globalResources
            // load textures from disk
            // upload textures
            // upload materials

        uint queryIndex = world.Query(Components::Material::mask, 0, true);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
#define UpdateMaterialsStepSize 1024
        ViewWorld& frameWorld = viewWorld;

        tf::Task task = subflow.for_each_index(uint(0), entityCount, UpdateMaterialsStepSize,
            [this, &world, &frameWorld, queryIndex](int i)
            {
                ZoneScopedN("UpdateMaterials");
                for (uint subQuery = 0; subQuery < UpdateMaterialsStepSize; subQuery++)
                {
                    auto& queryResult = world.frameQueries[queryIndex];
                    if (i + subQuery > queryResult.size() - 1)
                        return;

                    HLSL::Material material;
                    {
                        Components::Material& materialCmp = queryResult[i + subQuery].Get<Components::Material>();

                        // Stable per-shader-bucket index (generic multi-shader GBuffer draw
                        // indirect): culling.hlsl routes this material's meshlets by this index
                        // instead of the old cutout/terrain bool flags. ~0 (same "not assigned"
                        // convention as the texture slots below) means no shader -- culling.hlsl
                        // skips drawing that meshlet entirely rather than falling back to a default.
                        material.shaderIndex = ~0;
                        if (materialCmp.shader.IsValid())
                        {
                            // The Shader entity carries its own path (set wherever it's created --
                            // CreateMaterials, the "New Terrain" menu, ...), so it self-registers in
                            // AssetLibrary from saved data alone: no pass needs to hardcode it ahead
                            // of time. Idempotent, so calling it every frame for every material is
                            // cheap and safe (same pattern as GetOrRegister itself).
                            Components::Shader& shaderCmp = materialCmp.shader.Get();
                            if (shaderCmp.path[0] != 0)
                                AssetLibrary::instance->Add(shaderCmp.path, shaderCmp.path, shaderCmp.id);
                            material.shaderIndex = frameWorld.shaderBucketRegistry.GetOrRegister(shaderCmp.id);
                        }
                        for (uint paramIndex = 0; paramIndex < HLSL::MaterialParametersCount; paramIndex++)
                        {
                            // memcpy ? it is even just a cashline 
                            material.parameters[paramIndex] = materialCmp.parameters[paramIndex];
                        }
                        bool materialReady = true;
                        for (uint texIndex = 0; texIndex < HLSL::MaterialTextureCount; texIndex++)
                        {
                            if (materialCmp.textures[texIndex] != entityInvalid)
                            {
                                Components::Texture& textureCmp = materialCmp.textures[texIndex].Get();
                                Resource* texture = AssetLibrary::instance->Get<Resource>(textureCmp.id);
                                if (!texture)
                                {
                                    materialReady = false;
                                    material.textures[texIndex] = ~0;
                                }
                                else
                                    material.textures[texIndex] = texture->srv.offset;
                            }
                            else
                                material.textures[texIndex] = ~0;
                        }
                    }
                    frameWorld.materials.AddOrUpdate(World::Entity(queryResult[i + subQuery].Get<Components::Entity>()), material);
                }
            }
        );
        return task;
    }

    tf::Task UpdateLights(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        // upload lights
        uint queryIndex = world.Query(Components::Light::mask, 0, true);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        uint entityStep = 1;
        ViewWorld& frameWorld = viewWorld;

        // upload lights : no need to schedule that before all other passes for the moment because
        // the proj and the planes are not used on the CPU
        // we will need to make the task preced watherver pass needs to have up to date camera data

        tf::Task task = subflow.emplace(
            [this, &world]()
            {
                ZoneScoped;
                uint queryIndex = world.Query(Components::Light::mask, 0, true);

                uint entityCount = (uint)world.frameQueries[queryIndex].size();
                uint entityStep = 1;
                ViewWorld& frameWorld = viewWorld;
                auto& queryResult = world.frameQueries[queryIndex];

                for (uint i = 0; i < entityCount; i++)
                {
                    auto& light = queryResult[i].Get<Components::Light>();
                    //auto& trans = queryResult[i].Get<Components::WorldMatrix>();

                    World::Entity ent = queryResult[i].Get<Components::Entity>();
                    float4x4 worldMatrix = ComputeWorldMatrix(ent);

                    HLSL::Light hlsllight;

                    float tableMultiplier = lightUnitTable[options.lightUnitsIndex].multiplier;
                    float unitMul = tableMultiplier > 0.0f ? tableMultiplier : options.customLightMultiplier;
                    hlsllight.pos = float4(worldMatrix[3].xyz, 1);
                    hlsllight.dir = float4(normalize(worldMatrix[2].xyz), 1);
                    hlsllight.color = float4(light.color.xyz * unitMul, light.color.w);
                    hlsllight.angle = light.angle;
                    hlsllight.range = light.range;
                    hlsllight.type = light.type;
                    hlsllight.size = light.size;
                    hlsllight.castShadow = light.castShadow ? 1u : 0u;

                    this->viewWorld.lights.Get().Add(hlsllight);
                }
            }
        ).name("Update lights");
        return task;
    }

    tf::Task UpdateCameras(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        // upload camera : no need to schedule that before all other passes for the moment because
        // the proj and the planes are not used on the CPU
        // we will need to make the task preced watherver pass needs to have up to date camera data

        tf::Task task = subflow.emplace(
            [this, &world]()
            {
                ZoneScoped;
                uint queryIndex = world.Query(Components::Camera::mask, 0, true);

                uint entityCount = (uint)world.frameQueries[queryIndex].size();
                uint entityStep = 1;
                ViewWorld& frameWorld = viewWorld;
                auto& queryResult = world.frameQueries[queryIndex];

                static HLSL::Camera hlslcamPrevious = {};
                if (!options.stopFrustumUpdate)
                {
                    if (this->viewWorld.cameras.GetPrevious().Size() > 0)
                    {
                        hlslcamPrevious = this->viewWorld.cameras.GetPrevious()[0];
                    }
                }

                for (uint i = 0; i < entityCount; i++)
                {
                    auto& cam = queryResult[i].Get<Components::Camera>();
                    auto& trans = queryResult[i].Get<Components::Transform>();
                    auto& mat = queryResult[i].Get<Components::WorldMatrix>();
                    float4x4 previousMat = mat.matrix;
                    mat.matrix = Matrix(trans.position, trans.rotation, trans.scale);

                    float4x4 proj = MatrixPerspectiveFovLH(cam.fovY * (3.14f / 180.0f), float(this->renderResolution.x) / float(this->renderResolution.y), cam.nearClip, cam.farClip, HLSL::reverseZ);
                    float4x4 viewProj = mul(inverse(mat.matrix), proj);
                    float4 worldPos = float4(mat.matrix[3].xyz, 1);
                    float4x4 previousViewProj = mul(inverse(previousMat), proj);
                    float4 previousWorldPos = float4(previousMat[3].xyz, 1);

                    float4 planes[6];
                    float3 worldCorners[8];

                    // compute planes
                    float4x4 matProj = mul(inverse(MatrixPerspectiveFovLH(cam.fovY * (3.14f / 180.0f), float(this->renderResolution.x) / float(this->renderResolution.y), cam.nearClip, cam.farClip, false)), mat.matrix);

                    //create the 8 points of a cube in unit-space
                    float4 cube[8];
                    cube[0] = float4(-1.0f, -1.0f, 0.0f, 1.0f); // xyz
                    cube[1] = float4(1.0f, -1.0f, 0.0f, 1.0f); // Xyz
                    cube[2] = float4(-1.0f, 1.0f, 0.0f, 1.0f); // xYz
                    cube[3] = float4(1.0f, 1.0f, 0.0f, 1.0f); // XYz
                    cube[4] = float4(-1.0f, -1.0f, 1.0f, 1.0f); // xyZ
                    cube[5] = float4(1.0f, -1.0f, 1.0f, 1.0f); // XyZ
                    cube[6] = float4(-1.0f, 1.0f, 1.0f, 1.0f); // xYZ
                    cube[7] = float4(1.0f, 1.0f, 1.0f, 1.0f); // XYZ

                    //transform all 8 points by the view/proj matrix. Doing this
                    //gives us that ACTUAL 8 corners of the frustum area.
                    float4 tmp;
                    for (int i = 0; i < 8; i++)
                    {
                        tmp = float4(mul(cube[i], matProj).vec);
                        worldCorners[i] = float3((tmp / tmp.w).vec);
                    }

                    //4. generate and store the 6 planes that make up the frustum
                    planes[0] = PlaneFromPoints(worldCorners[0], worldCorners[1], worldCorners[2]); // Near
                    planes[1] = PlaneFromPoints(worldCorners[6], worldCorners[7], worldCorners[5]); // Far
                    planes[2] = PlaneFromPoints(worldCorners[2], worldCorners[6], worldCorners[4]); // Left
                    planes[3] = PlaneFromPoints(worldCorners[7], worldCorners[3], worldCorners[5]); // Right
                    planes[5] = PlaneFromPoints(worldCorners[1], worldCorners[0], worldCorners[4]); // Bottom
                    planes[4] = PlaneFromPoints(worldCorners[2], worldCorners[3], worldCorners[6]); // Top

                    HLSL::Camera hlslcam;

                    hlslcam.view = inverse(mat.matrix);
                    hlslcam.view_inv = mat.matrix;
                    hlslcam.proj = proj;
                    hlslcam.proj_inv = inverse(proj);
                    hlslcam.viewProj = viewProj;
                    // inverse(view*proj) built analytically as proj_inv * camWorldMatrix: a general
                    // fp32 inverse of the composed matrix degrades with the camera translation magnitude.
                    hlslcam.viewProj_inv = mul(hlslcam.proj_inv, mat.matrix);
                    hlslcam.planes[0] = planes[0];
                    hlslcam.planes[1] = planes[1];
                    hlslcam.planes[2] = planes[2];
                    hlslcam.planes[3] = planes[3];
                    hlslcam.planes[4] = planes[4];
                    hlslcam.planes[5] = planes[5];
                    hlslcam.worldPos = worldPos;

                    hlslcam.previousViewProj = previousViewProj;
                    hlslcam.previousViewProj_inv = mul(hlslcam.proj_inv, previousMat);
                    hlslcam.previousWorldPos = previousWorldPos;

                    hlslcam.sizeCulling = 1;
                    hlslcam.fovY = cam.fovY;
                    hlslcam.nearClip = cam.nearClip;
                    hlslcam.farClip = cam.farClip;
                    /*
                    hlslcam.camClipsExt.x = 1.0f - cam.farClip / cam.nearClip;
                    hlslcam.camClipsExt.y = cam.farClip / cam.nearClip;
                    hlslcam.camClipsExt.z = (ATMO_VOLUME_SIZE_Z - 1) / log2(ATMO_VOLUME_SPECIAL_NEAR / farClip);
                    hlslcam.camClipsExt.w = ATMO_VOLUME_SIZE_Z;
                    */

                    this->viewWorld.cameras.Get().Add(hlslcam);

                    // The editor gizmo must use the camera we actually render with,
                    // which is cameras.Get()[0] (the first/main camera). Loaded scenes
                    // can now contain extra static camera entities, so guard against
                    // overwriting this with the last camera in the query.
                    if (i == 0)
                    {
                        editorState.cameraView = mat.matrix;
                        editorState.cameraProj = hlslcam.proj;

                        mainCamera = hlslcam;
                    }
                }
                this->viewWorld.cameras.Get().Add(hlslcamPrevious);

            }
        ).name("Update cameras");

        return task;
    }

    tf::Task UpdateRenderSettings(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        tf::Task task = subflow.emplace(
            [this, &world]()
            {
                ZoneScoped;

                HLSL::AtmosphericScatteringParameters& as = atmospehricScattering.asparams;
                HLSL::PostProcessParameters& pp = postProcess.ppparams;

                if (!baseSettingsCaptured)
                {
                    baseSettings.density = as.density;
                    baseSettings.luminosity = as.luminosity;
                    baseSettings.specialNear = as.specialNear;
                    baseSettings.heightFalloff = as.heightFalloff;
                    baseSettings.noiseFrequency = as.noiseFrequency;
                    baseSettings.noiseThresholdLow = as.noiseThresholdLow;
                    baseSettings.noiseThresholdHigh = as.noiseThresholdHigh;
                    baseSettings.animationSpeed = as.animationSpeed;
                    baseSettings.expoMul = pp.expoMul;
                    baseSettings.expoAdd = pp.expoAdd;
                    baseSettings.P = pp.P;
                    baseSettings.a = pp.a;
                    baseSettings.m = pp.m;
                    baseSettings.l = pp.l;
                    baseSettings.c = pp.c;
                    baseSettings.b = pp.b;
                    baseSettingsCaptured = true;
                }

                // TODO : when multiple camera switch will work. Get the currently 'main camera' instead of 0
                uint camQuery = world.Query(Components::Camera::mask, 0, true);
                auto& cams = world.frameQueries[camQuery];
                if (cams.empty())
                    return;
                World::Entity camEnt = cams[0].Get<Components::Entity>();
                float3 cameraPos = ComputeWorldMatrix(camEnt)[3].xyz;

                // Gather volumes and blend low -> high priority so higher priority wins on overlap.
                uint volQuery = world.Query(Components::RenderSettingsVolume::mask, 0, true);
                std::vector<World::Entity> volumes;
                for (auto& e : world.frameQueries[volQuery])
                    volumes.push_back(e);
                std::sort(volumes.begin(), volumes.end(),
                    [](World::Entity x, World::Entity y)
                    {
                        return x.Get<Components::RenderSettingsVolume>().priority
                             < y.Get<Components::RenderSettingsVolume>().priority;
                    });

                Components::RenderSettingsVolume result = baseSettings;
                for (World::Entity e : volumes)
                {
                    auto& v = e.Get<Components::RenderSettingsVolume>();

                    float w = 0.0f;
                    if (v.shape == 2) // Global / unbound
                    {
                        w = 1.0f;
                    }
                    else
                    {
                        float4x4 wm = ComputeWorldMatrix(e);
                        float3 center = wm[3].xyz;
                        float distOutside = 0.0f;
                        if (v.shape == 1) // Sphere
                        {
                            float dist = length(cameraPos - center);
                            distOutside = std::max(dist - v.radius, 0.0f);
                        }
                        else // Box (OBB): half-extent along each axis = length(scaled axis) * 0.5
                        {
                            float3 d = cameraPos - center;
                            float3 ax0 = wm[0].xyz, ax1 = wm[1].xyz, ax2 = wm[2].xyz;
                            float over0 = std::max(fabsf((float)dot(d, normalize(ax0))) - (float)length(ax0) * 0.5f, 0.0f);
                            float over1 = std::max(fabsf((float)dot(d, normalize(ax1))) - (float)length(ax1) * 0.5f, 0.0f);
                            float over2 = std::max(fabsf((float)dot(d, normalize(ax2))) - (float)length(ax2) * 0.5f, 0.0f);
                            distOutside = length(float3(over0, over1, over2));
                        }
                        if (v.blendDistance > 0.0f)
                            w = std::min(std::max(1.0f - distOutside / v.blendDistance, 0.0f), 1.0f);
                        else
                            w = distOutside <= 0.0f ? 1.0f : 0.0f;
                    }
                    if (w <= 0.0f)
                        continue;

                    #define RSV_BLEND(bit, field) if (v.overrides & Components::bit) result.field += (v.field - result.field) * w;
                    RSV_BLEND(RSO_density, density);
                    RSV_BLEND(RSO_luminosity, luminosity);
                    RSV_BLEND(RSO_specialNear, specialNear);
                    RSV_BLEND(RSO_heightFalloff, heightFalloff);
                    RSV_BLEND(RSO_noiseFrequency, noiseFrequency);
                    RSV_BLEND(RSO_noiseThresholdLow, noiseThresholdLow);
                    RSV_BLEND(RSO_noiseThresholdHigh, noiseThresholdHigh);
                    RSV_BLEND(RSO_animationSpeed, animationSpeed);
                    RSV_BLEND(RSO_expoMul, expoMul);
                    RSV_BLEND(RSO_expoAdd, expoAdd);
                    RSV_BLEND(RSO_P, P);
                    RSV_BLEND(RSO_a, a);
                    RSV_BLEND(RSO_m, m);
                    RSV_BLEND(RSO_l, l);
                    RSV_BLEND(RSO_c, c);
                    RSV_BLEND(RSO_b, b);
                    #undef RSV_BLEND
                }

                // Write the blended artistic fields back; leave the GPU-index fields to the passes.
                as.density = result.density;
                as.luminosity = result.luminosity;
                as.specialNear = result.specialNear;
                as.heightFalloff = result.heightFalloff;
                as.noiseFrequency = result.noiseFrequency;
                as.noiseThresholdLow = result.noiseThresholdLow;
                as.noiseThresholdHigh = result.noiseThresholdHigh;
                as.animationSpeed = result.animationSpeed;
                // The composite pass samples the froxel volume and needs the same specialNear
                // it was built with; copied here (single-threaded update phase) to avoid racing
                // the AtmosphericScattering pass's writes to its own asparams during render.
                postProcessHalfRes.asparams = as;

                pp.expoMul = result.expoMul;
                pp.expoAdd = result.expoAdd;
                pp.P = result.P;
                pp.a = result.a;
                pp.m = result.m;
                pp.l = result.l;
                pp.c = result.c;
                pp.b = result.b;
            }
        ).name("Update render settings");

        return task;
    }

    tf::Task UploadAndSetup(World& world, tf::Subflow& subflow)
    {
        tf::Task task = subflow.emplace(
            [this]()
            {
                ZoneScoped;
                this->viewWorld.cameras.Get().Upload();
                this->viewWorld.lights.Get().Upload();
                this->viewContext.instancesCulledArgs.Resize(this->viewWorld.instances.Size());

                // See ShaderBucketRegistry::BuildFrameSnapshot: NOT viewWorld.meshletsCount, which
                // only sums instances touched *this* frame and would silently undercount once
                // instances settle into the loaded-and-not-dirty steady state.
                uint totalMeshlets = this->viewWorld.shaderBucketRegistry.BuildFrameSnapshot(this->viewWorld.shaderBucketOffsets.Get(), this->viewWorld.shaderBuckets);

                this->viewContext.meshletsCulledArgs.Resize(totalMeshlets);
                this->viewContext.meshletsCulledArgsSorted.Resize(totalMeshlets);
                this->viewContext.meshletBuckets.Resize(totalMeshlets);
                // CullingInstances (culling.hlsl) writes one meshletsToCull entry per meshlet of
                // EVERY instance that survives frustum/occlusion/distance culling this frame -- that
                // can be any currently-loaded instance, not just ones touched this frame, so this
                // must share totalMeshlets' persistent accounting (see the comment above), not
                // viewWorld.meshletsCount. Sizing it off the per-frame-touched count undersized the
                // buffer once enough settled (loaded, non-dirty) instances existed simultaneously --
                // e.g. terrain nodes, which stop re-touching every frame once baked -- causing
                // out-of-bounds atomic-counter writes that flickered.
                this->viewContext.meshletsToCull.Resize(totalMeshlets);
                this->viewContext.meshletsCounter.Resize((uint)this->viewWorld.shaderBuckets.size());
                this->raytracingContext.instancesRayTracing.Resize(this->viewWorld.instances.Size());

                this->viewWorld.commonResourcesIndices = SetupCommonResourcesParams();
                this->viewContext.viewContext = SetupViewContextParams();
                this->raytracingContext.rtParameters = SetupRayTracingContextParams();
                this->editorContext.editorContext = SetupEditorParams();
            }
        ).name("upload instances buffer");

        return task;
    }
};

class Editor : public Pass
{
public:
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        commandBuffer->cmd->OMSetRenderTargets(1, &GPU::instance->backBuffer->rtv.handle, false, nullptr);

        UI::instance->FrameRender(commandBuffer->cmd);

        GPU::instance->backBuffer->Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

        Profiler::instance->EndProfilerFrame(commandBuffer.Get());
        Close();
    }
};

class EditorView : public View
{
public:
    Editor editor;

    void On(uint2 _displayResolution, uint2 _renderResolution) override
    {
        ZoneScoped;
        // avoid creating rt and context buffers for this view
        //View::On(_displayResolution, _renderResolution);
        renderResolution = _renderResolution;
        displayResolution = _displayResolution;

        editor.On(this, GPU::instance->graphicQueue, "editor", nullptr, nullptr);
    }

    void Off() override
    {
        ZoneScoped;
        editor.Off();
    }

    tf::Task Schedule(World& world, tf::Subflow& subflow) override
    {
        ZoneScoped;
        SUBTASKVIEWPASS(editor);
        return editorTask;
    }

};



// https://www.youtube.com/watch?v=cGB3wT0U5Ao&ab_channel=CppCon
// use DAG
class Renderer
{
public:
    static Renderer* instance;
    ConstantBuffer constantBuffer;
    MeshStorage meshStorage;
    TextureStorage textureStorage;
    ShaderStorage shaderStorage;
    MainView mainView;
    EditorView editorView;
    SubmissionList submissions; // shared by every view; passes reach it through view->submissions, not a global

    void BuildSubmissions()
    {
        submissions.Clear();
        mainView.submissions = &submissions;
        editorView.submissions = &submissions;
    }

    void On(uint2 _displayResolution)
    {
        ZoneScoped;
        instance = this;

        constantBuffer.On();
        meshStorage.On();
        textureStorage.On();
        shaderStorage.On();
        BuildSubmissions();
        mainView.On(_displayResolution, float2(_displayResolution) * 1.f);
        editorView.On(_displayResolution, _displayResolution);

        endOfLastFrame = &editorView.editor.commandBuffer;
    }

    void Off()
    {
        ZoneScoped;
        editorView.Off();
        mainView.Off();
        meshStorage.Off();
        textureStorage.Off();
        shaderStorage.Off();
        constantBuffer.Off();
        instance = nullptr;
    }

    void Schedule(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        submissions.Reset();
        AssetLibrary::instance->Close();
        AssetLibrary::instance->Execute();

        Profiler::instance->frameData.instancesCount = mainView.viewWorld.instances.Size();
        Profiler::instance->frameData.meshletsCount = mainView.viewWorld.meshletsCount;
        Profiler::instance->frameData.verticesCount = 0;

        constantBuffer.Reset();

        auto mainViewEndTask = mainView.Schedule(world, subflow);
        auto editorViewEndTask = editorView.Schedule(world, subflow);

        SUBTASKRENDERER(ExecuteFrame);
        SUBTASKRENDERER(Cleanup);
        SUBTASKRENDERER(WaitFrame);
        SUBTASKRENDERER(PresentFrame);

        SUBTASKRENDERER(ApplyPendingQualityChange);

        ExecuteFrame.succeed(mainViewEndTask, editorViewEndTask);
        ExecuteFrame.precede(Cleanup);
        Cleanup.precede(WaitFrame);
        WaitFrame.precede(PresentFrame);
        PresentFrame.precede(ApplyPendingQualityChange);
    }

    void ExecuteFrame()
    {
        ZoneScoped;
        submissions.DrainRemaining();
    }

    void Cleanup()
    {
        ZoneScoped;
        Resource::ReleaseResources();
        // Recycle pooled upload buffers on the same schedule as the deferred frees above.
        GPU::instance->uploadBufferPool.Recycle(GPU::instance->frameNumber);
    }

    void ExecuteImmediate(ID3D12GraphicsCommandList* cmd, ID3D12CommandQueue* queue)
    {
        ZoneScoped;

        WaitFrame();

        ID3D12CommandList* lists[] = { cmd };
        queue->ExecuteCommandLists(1, lists);

        // Wait for completion
        ID3D12Fence* fence = nullptr;
        UINT64 fenceValue = 0;
        HRESULT hr = GPU::instance->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            cmd->Release();
            return;
        }
        fenceValue = 1;
        queue->Signal(fence, fenceValue);
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (event)
        {
            fence->SetEventOnCompletion(fenceValue, event);
            WaitForSingleObject(event, INFINITE);
            CloseHandle(event);
        }
        fence->Release();
    }

    void WaitFrame()
    {
        ZoneScoped;

        HRESULT hr;
        // if the current fence value is still less than "fenceValue", then we know the GPU has not finished executing
        // the command queue since it has not reached the "commandQueue->Signal(fence, fenceValue)" command
        Fence& previousFrame = endOfLastFrame->Get(GPU::instance->frameIndex ? 0 : 1).passEnd;
        auto v = previousFrame.fence->GetCompletedValue();
        if (v < previousFrame.fenceValue)
        {
            // we have the fence create an event which is signaled once the fence's current value is "fenceValue"
            hr = previousFrame.fence->SetEventOnCompletion(previousFrame.fenceValue, previousFrame.fenceEvent);
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
                return;
            }

            // We will wait until the fence has triggered the event that it's current value has reached "fenceValue". once it's value
            // has reached "fenceValue", we know the command queue has finished executing
            WaitForSingleObject(previousFrame.fenceEvent, 10000);
        }
    }

    void Flush()
    {
        ZoneScoped;
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            GPU::instance->FrameStart();
            WaitFrame();
            PresentFrame();
        }
    }

    void PresentFrame()
    {
        ZoneScoped;
        HRESULT hr;

        if (GPU::instance->swapChain != nullptr)
        {
            // present the current backbuffer
            if (GPU::instance->features.vSync)
            {
                // Lock to screen refresh rate.
                hr = GPU::instance->swapChain->Present(1, 0);
            }
            else
            {
                // Present as fast as possible.
                // DXGI_PRESENT_ALLOW_TEARING is not compatible with fullscreen exclusive
                hr = GPU::instance->swapChain->Present(0, GPU::instance->features.fullscreen ? 0 : DXGI_PRESENT_ALLOW_TEARING);
            }
        }
        else
            IOs::Log("NO SWAPCHAIN");
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
        }
    }

    void ApplyPendingQualityChange()
    {
        DLSS& dlss = mainView.dlss;
        if (!dlss.qualityChangePending)
            return;

        Flush();

        if (dlss.dlss_feature)
        {
            NVSDK_NGX_D3D12_ReleaseFeature(dlss.dlss_feature);
            dlss.dlss_feature = nullptr;
        }

        HLSL::Upscaling savedUpscaling = mainView.upscaling;
        uint2 disp = mainView.displayResolution;

        editorView.Off();
        mainView.Off();
        dlss.perf_quality = dlss.requestedQuality;
        dlss.initialized = false;

        Resource::ReleaseResources(true);
        GPU::instance->uploadBufferPool.Recycle(GPU::instance->frameNumber);

        BuildSubmissions(); // rebuild from scratch: the views' Off()/On() below re-register the passes
        mainView.On(disp, disp);
        editorView.On(disp, disp);
        mainView.upscaling = savedUpscaling;
        endOfLastFrame = &editorView.editor.commandBuffer;

        GPU::instance->descriptorHeap.CheckSlotsValidity();

        dlss.qualityChangePending = false;
    }
};
Renderer* Renderer::instance;
