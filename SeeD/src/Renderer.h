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
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
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

// life time : frame
struct ViewWorld
{
    HLSL::CommonResourcesIndices commonResourcesIndices;
    PerFrame<StructuredUploadBuffer<HLSL::Camera>> cameras;
    PerFrame<StructuredUploadBuffer<HLSL::Light>> lights;
    DirtyTrackingStructuredBuffer<World::Entity, HLSL::Material> materials;
    DirtyTrackingStructuredBuffer<World::Entity, HLSL::Instance> instances;
    std::vector<HLSL::Instance> instancesReadBackDebug;

    std::atomic<uint> meshletsCount;

    // Static instance used by the callback function pointer (non-capturing lambda)
    static ViewWorld* instance;

    void On()
    {
        ZoneScoped;
        // store instance pointer so we can use a non-capturing lambda (convertible to function pointer)
        instance = this;

        // Assign a non-capturing lambda that uses the static instance pointer.
        // Non-capturing lambdas can convert to function pointers of the appropriate type.
        Components::RemoveCallback[Components::Instance::bucketIndex] = [](EntityBase entity)
        {
            if (ViewWorld::instance)
            {
                ViewWorld::instance->instances.Remove(World::Entity(entity));
            }
        };
    }

    void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            cameras.Get(i).Release();
            lights.Get(i).Release();
        }
        materials.Release();
        instances.Release();
    }
};

// Define the static instance
ViewWorld* ViewWorld::instance = nullptr;

// life time : view (only updated on GPU)
struct ViewContext
{
    HLSL::ViewContext viewContext; // to bind to rootSig

    StructuredBuffer<HLSL::Camera> camera;
    StructuredBuffer<HLSL::Light> lights;
    StructuredBuffer<HLSL::InstanceCullingDispatch> instancesCulledArgs;
    StructuredBuffer<HLSL::GroupedCullingDispatch> meshletsToCull;
    StructuredBuffer<HLSL::MeshletDrawCall> meshletsCulledArgs;
    StructuredBuffer<HLSL::MeshletDrawCall> meshletsCulledArgsSorted; // front-to-back ordered copy
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
    // create a indirect buffer to store line to be drawn buy this debug shader 
    HLSL::EditorContext editorContext;
    StructuredBuffer<HLSL::IndirectCommand> indirectDebugBuffer;
    StructuredBuffer<HLSL::Vertex> indirectDebugVertices;
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
        rtParameters.spacialRadius = 0.0f;
        rtParameters.spacialSampleCount = 8;
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
        rtParameters.throughputThreshold = 0.01f;
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
    virtual void Execute() = 0;
    
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
        commonResourcesIndices.instancesHeapIndex = viewWorld.instances.GetResource().uav.offset;
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
        viewContextParams.albedoIndex = GetRegisteredResource("albedo").srv.offset;
        viewContextParams.metalnessIndex = GetRegisteredResource("metalness").srv.offset;
        viewContextParams.roughnessIndex = GetRegisteredResource("roughness").srv.offset;
        viewContextParams.normalIndex = GetRegisteredResource("normal").srv.offset;
        viewContextParams.motionIndex = GetRegisteredResource("motion").srv.offset;
        viewContextParams.objectIDIndex = GetRegisteredResource("objectID").uav.offset;
        viewContextParams.instanceIDIndex = GetRegisteredResource("instanceID").uav.offset;
        viewContextParams.overdrawIndex = GetRegisteredResource("overdraw").uav.offset;
        viewContextParams.depthIndex = GetRegisteredResource("depth").srv.offset;
        viewContextParams.reverseZ = true;
        viewContextParams.HZB = GetRegisteredResource("depthDownSample").srv.offset;
        viewContextParams.HZBMipCount = GetRegisteredResource("depthDownSample").GetResource()->GetDesc().MipLevels;
        viewContextParams.textureLODBias = -1.0f;
        viewContextParams.sortMaxDistance = options.sortMaxDistance;
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

        editorContextParams.rays = options.debugMode == Options::DebugMode::ray;
        editorContextParams.boundingVolumes = options.debugMode == Options::DebugMode::boundingSphere;
        editorContextParams.albedo = options.debugDraw == Options::DebugDraw::albedo;
        editorContextParams.normals = options.debugDraw == Options::DebugDraw::normals;
        editorContextParams.clusters = options.debugDraw == Options::DebugDraw::clusters;
        editorContextParams.lighting = options.debugDraw == Options::DebugDraw::lighting;
        editorContextParams.GIprobes = options.debugDraw == Options::DebugDraw::GIprobes;
        editorContextParams.GIBounces = options.debugDraw == Options::DebugDraw::GIBounces;
        editorContextParams.GIAlbedo = options.debugDraw == Options::DebugDraw::GIAlbedo;
        editorContextParams.GINormals = options.debugDraw == Options::DebugDraw::GINormals;
        editorContextParams.overdraw = options.debugDraw == Options::DebugDraw::overdraw;
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
    View* view;
    UINT64 hash;
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
    Resource& Get()
    {
        lock.lock();
        Resource& res = view->resources[hash];
        lock.unlock();
        return res;
    }
};
std::mutex ViewResource::lock;

class Pass
{
public:
    View* view;
    PerFrame<CommandBuffer> commandBuffer;
    PerFrame<CommandBuffer>* dependency = nullptr;
    PerFrame<CommandBuffer>* dependency2 = nullptr;

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
        if (commandBuffer->open)
            IOs::Log("{} OPEN !!", name.c_str());

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
        commandBuffer->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&commandBuffer->cmd);
        commandBuffer->queue->Signal(commandBuffer->passEnd.fence, ++commandBuffer->passEnd.fenceValue);
    }

    void ExecuteNow()
    {
        ZoneScoped;
        if (commandBuffer->open)
            IOs::Log("{} OPEN !!", name.c_str());

        commandBuffer->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&commandBuffer->cmd);
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

        seedAssert(view->raytracingContext.instancesRayTracing.Size() == instanceCount);
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
            // The histogram + per-meshlet buckets were filled inside CullingMeshlets above.
            // Reorder the culled meshlet draw list front-to-back (prefix sum + scatter) so the
            // gBuffer's early-Z rejects far fragments first, cutting overdraw.
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
        else
        {
            view->viewContext.meshletsCulledArgs.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }

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
    ViewResource roughness;
    ViewResource depth;
    ViewResource motion;
    ViewResource objectID;
    ViewResource instanceID;
    ViewResource overdraw;
    Components::Handle<Components::Shader> meshShader;
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
        normal.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R16G16B16A16_FLOAT, "normal");
        metalness.Register("metalness", view);
        metalness.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R8_UNORM, "metalness");
        roughness.Register("roughness", view);
        roughness.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R8_UNORM, "roughness");
        depth.Register("depth", view);
        motion.Register("motion", view);
        motion.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R16G16_FLOAT, "motion");
        objectID.Register("objectID", view);
        objectID.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R32_UINT, "objectID");
        instanceID.Register("instanceID", view);
        instanceID.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R32_UINT, "instanceID");
        overdraw.Register("overdraw", view);
        overdraw.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R32_UINT, "overdraw"); // per-pixel atomic counter for the overdraw heatmap
        meshShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\mesh.hlsl|DefaultG");

        Open();
        albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        roughness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        objectID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        instanceID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandBuffer.Get().cmd->DiscardResource(albedo.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(specularAlbedo.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(normal.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(metalness.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(roughness.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(motion.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(objectID.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(instanceID.Get().GetResource(), nullptr);
        commandBuffer.Get().cmd->DiscardResource(overdraw.Get().GetResource(), nullptr);
        albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        roughness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        objectID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        instanceID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        Close();
        ExecuteNow();
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
        roughness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        objectID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        instanceID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);

        // Overdraw heatmap: clear the counter then make it a UAV the pixel shader atomically increments.
        // Only paid for when the overdraw debug view is active.
        bool overdrawEnabled = options.debugDraw == Options::DebugDraw::overdraw;
        if (overdrawEnabled)
        {
            float clearOverdraw[4] = { 0, 0, 0, 0 };
            overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
            commandBuffer->cmd->ClearRenderTargetView(overdraw.Get().rtv.handle, clearOverdraw, 0, nullptr);
            overdraw.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        Resource rts[] = { albedo.Get(), specularAlbedo.Get(), normal.Get(), metalness.Get(), roughness.Get(), motion.Get(), objectID.Get(), instanceID.Get()};
        SetupView(view, rts, ARRAYSIZE(rts), true, &depth.Get(), true, false);

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);

        Shader& shader = *AssetLibrary::instance->Get<Shader>(meshShader.Get().id, true);
        commandBuffer->SetGraphic(shader);

        commandBuffer->cmd->SetGraphicsRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetGraphicsRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetGraphicsRootConstantBufferView(EditorContextRegister, editorContextAddress);

        auto& drawArgs = options.frontToBackSort ? view->viewContext.meshletsCulledArgsSorted : view->viewContext.meshletsCulledArgs;
        uint maxDraw = drawArgs.Size();
        commandBuffer->cmd->ExecuteIndirect(shader.commandSignature, maxDraw, drawArgs.GetResourcePtr(), 0, view->viewContext.meshletsCounter.GetResourcePtr(), 0);

        albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        specularAlbedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        roughness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        objectID.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
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

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);

        rayDispatchShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\raytracing2.hlsl|Update");
        resolveShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\SHARCResolve.hlsl|sharcResolve");

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

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);
        auto editorContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->editorContext.editorContext);
        auto raytracingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);

        // Trace rays (+ temporal ReSTIR)
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


        view->raytracingContext.SHARCAccumulation.Barrier(commandBuffer.Get());


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
        lighted.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R16G16B16A16_FLOAT, "lighted");
        specularHitDistance.Register("specularHitDistance", view);
        specularHitDistance.Get().CreateRenderTarget(view->renderResolution, DXGI_FORMAT_R16_FLOAT, "specularHitDistance");
        albedo.Register("albedo", view);
        depth.Register("depth", view);
        normal.Register("normal", view);
        rayDispatchShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\raytracing2.hlsl|Query");
        applyLightingShader.GetPermanent().id = AssetLibrary::instance->AddHardCoded("src\\Shaders\\lighting.hlsl|Lighting");
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

        // Lighting (+ spacial ResTIR)
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

        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

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
            Shader& debugBuffers = *AssetLibrary::instance->Get<Shader>(debugBuffersShader.Get().id, true);
            commandBuffer->SetCompute(debugBuffers);
            commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(EditorContextRegister, editorContextAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, rtParametersAddress);
            commandBuffer->cmd->Dispatch(debugBuffers.DispatchX(view->renderResolution.x), debugBuffers.DispatchY(view->renderResolution.y), 1);

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
        froxelsData[1].resolution[0] = (uint)atmosphericScatteringHistoryFroxels.Get().GetResource()->GetDesc().Width;
        froxelsData[1].resolution[1] = (uint)atmosphericScatteringHistoryFroxels.Get().GetResource()->GetDesc().Height;
        froxelsData[1].resolution[2] = atmosphericScatteringHistoryFroxels.Get().GetResource()->GetDesc().DepthOrArraySize;
        froxelsData[1].index = atmosphericScatteringHistoryFroxels.Get().uav.offset;
        froxelsBuffer.Get().UploadElements(froxelsData, ARRAYSIZE(froxelsData), 0, commandBuffer.Get());
        froxelsBuffer.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        Close();

        ExecuteNow();
    }
    virtual void Off() override
    {
        Pass::Off();
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
        asparams.froxelsIndex = froxelsBuffer.Get().uav.offset;
        asparams.currentFroxelIndex = 0;
        asparams.historyFroxelIndex = 1;

        auto atmosphericScatteringAddress = ConstantBuffer::instance->PushConstantBuffer(&asparams);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom2Register, atmosphericScatteringAddress);

        D3D12_DISPATCH_RAYS_DESC drd = atmosphericScattering.GetRTDesc();
        drd.Width = (uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Width;
        drd.Height = (uint)atmosphericScatteringFroxels.Get().GetResource()->GetDesc().Height;
        drd.Depth = atmosphericScatteringFroxels.Get().GetResource()->GetDesc().DepthOrArraySize;

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

    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
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
    }
    virtual void Off() override
    {
        Pass::Off();
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

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);

        pphrparams.froxelsIndex = atmosphericScatteringFroxelsBuffer.Get().uav.offset;
        pphrparams.atmosphericScatteringIndex = 0;
        pphrparams.lightedIndex = lighted.Get().uav.offset;
        pphrparams.transparencyLayerIndex = transparencyLayer.Get().uav.offset;

        Shader& postProcessHalfRes = *AssetLibrary::instance->Get<Shader>(postProcessHalfResShader.Get().id, true);
        commandBuffer->SetCompute(postProcessHalfRes);
        commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&pphrparams));
        commandBuffer->cmd->Dispatch(postProcessHalfRes.DispatchX(view->renderResolution.x), postProcessHalfRes.DispatchY(view->renderResolution.y), 1);

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
        ppparams.expoMul = 4;


        Open();
        postProcessed.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandBuffer.Get().cmd->DiscardResource(postProcessed.Get().GetResource(), nullptr);
        postProcessed.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Close();
        ExecuteNow();
    }
    virtual void Off() override
    {
        Pass::Off();
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

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld.commonResourcesIndices);
        auto viewContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewContext.viewContext);

        if (view->upscaling == HLSL::Upscaling::taa)
        {
            taaparams.lightedIndex = lighted.Get().uav.offset;
            taaparams.historyIndex = history.Get().uav.offset;

            Shader& TAA = *AssetLibrary::instance->Get<Shader>(TAAShader.Get().id, true);
            commandBuffer->SetCompute(TAA);
            commandBuffer->cmd->SetComputeRootConstantBufferView(CommonResourcesIndicesRegister, commonResourcesIndicesAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(ViewContextRegister, viewContextAddress);
            commandBuffer->cmd->SetComputeRootConstantBufferView(Custom1Register, ConstantBuffer::instance->PushConstantBuffer(&taaparams));
            commandBuffer->cmd->Dispatch(TAA.DispatchX(view->renderResolution.x), TAA.DispatchY(view->renderResolution.y), 1);


            history.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            commandBuffer->cmd->CopyResource(history.Get().GetResource(), lighted.Get().GetResource());
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
    ViewResource roughness;
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

    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        albedo.Register("albedo", view);
        specularAlbedo.Register("specularAlbedo", view);
        normal.Register("normal", view);
        roughness.Register("roughness", view);
        motion.Register("motion", view);
        depth.Register("depth", view);
        lighted.Register("lighted", view);
        transparencyLayer.Register("transparencyLayer", view);
        specularHitDistance.Register("specularHitDistance", view);
        upscaled.Register("upscaled", view);
        upscaled.Get().CreateRenderTarget(view->displayResolution, DXGI_FORMAT_R16G16B16A16_FLOAT, "upscaled"); // must be the same as Lighted input
    }
    virtual void Off() override
    {
        Pass::Off();
        ZoneScoped;
        NVSDK_NGX_D3D12_Shutdown1(GPU::instance->device);
    }
    void Setup(View* view) override
    {
        ZoneScoped;

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
        feature_common_info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;// NVSDK_NGX_LOGGING_LEVEL_VERBOSE;
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
        NGX_DLSS_GET_OPTIMAL_SETTINGS(ngx_parameters, displayResolution.x, displayResolution.y, perf_quality, &renderWidth, &renderHeight, &renderMaxWidth, &renderMaxHeight, &renderMinWidth, &renderMinHeight, &sharpness);

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

            if (upscalingPreviousSetting != view->upscaling)
            {
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
                    dlssd_create_params.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode::NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;
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
                dlss_eval_params.pInNormals = normal.Get().GetResource();
                dlss_eval_params.pInDiffuseAlbedo = albedo.Get().GetResource();
                dlss_eval_params.pInSpecularAlbedo = specularAlbedo.Get().GetResource();
                dlss_eval_params.pInRoughness = roughness.Get().GetResource();
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
            }
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


#define SUBTASKVIEWPASS(pass) tf::Task pass##Task = subflow.emplace([this](){this->pass.Render(this);}).name(#pass)
#define SUBTASKPASS(pass) tf::Task pass##Task = subflow.emplace([this](){this->pass.Render(nullptr);}).name(#pass)
#define SUBTASKRENDERERWORLD(pass) tf::Task pass = subflow.emplace([this, world](){this->pass(world);}).name(#pass)
#define SUBTASKRENDERER(pass) tf::Task pass = subflow.emplace([this](){this->pass();}).name(#pass)

// a view per type of render ? one for main view, one for cubemap, one for minimap ?
// main view render should always be the last one to render ?
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


    void On(uint2 _displayResolution, uint2 _renderResolution) override
    {
        ZoneScoped;
        dlss.CreateDLSS(this, _displayResolution, _renderResolution);

        View::On(_displayResolution, _renderResolution);

        hzb.On(this, GPU::instance->graphicQueue, "hzb", nullptr, nullptr);
        structuredCommandBufferUpdate.On(this, GPU::instance->computeQueue, "structuredCommandBufferUpdate", nullptr, nullptr);
        gpuDebugInit.On(this, GPU::instance->graphicQueue, "gpuDebugInit", &hzb.commandBuffer, nullptr);
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

        tf::Task uploadAndSetup = UploadAndSetup(world, subflow);

        SUBTASKVIEWPASS(gpuDebugInit);
        SUBTASKVIEWPASS(structuredCommandBufferUpdate);
        SUBTASKVIEWPASS(hzb);
        SUBTASKVIEWPASS(skinning);
        SUBTASKVIEWPASS(particles);
        SUBTASKVIEWPASS(spawning);
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
        // no need to put unnecessary dependencies on upload and setup (passes that do not use the view world data)
        // weeeelllll for all passes that use the camera need to wait for upload and setup to be sure the camera data is updated (just in case the buffers used are new because they are bigger)
        uploadAndSetup.precede(skinningTask, accelerationStructureTask, cullingTask, zPrepassTask, gBuffersTask, lightingProbesTask, lightingTask, atmospehricScatteringTask, postProcessHalfResTask, forwardTask, dlssTask, postProcessTask);

        presentTask.succeed(uploadAndSetup,
            structuredCommandBufferUpdateTask,
            hzbTask,
            skinningTask,
            particlesTask,
            spawningTask,
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

    void Execute() override
    {
        ZoneScoped;
        hzb.Execute();
        structuredCommandBufferUpdate.Execute();
        gpuDebugInit.Execute();
        skinning.Execute();
        particles.Execute();
        spawning.Execute();
        culling.Execute();
        zPrepass.Execute();
        accelerationStructure.Execute();
        atmospehricScattering.Execute();
        gBuffers.Execute();
        lightingProbes.Execute();
        lighting.Execute();
        postProcessHalfRes.Execute();
        forward.Execute();
        gpuDebug.Execute();
        dlss.Execute();
        postProcess.Execute();
        present.Execute();
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

#define UpdateInstancesStepSize 512 
        ViewWorld& frameWorld = viewWorld;
        
        uint instanceQueryIndex = world.Query(Components::Instance::mask | Components::WorldMatrix::mask, 0);
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
                    if (loaded && !dirty)
                        continue;

                    Components::Instance& instanceCmp = slot.Get<Components::Instance>();

                    World::Entity ent = World::Entity(slot.Get<Components::Entity>());
                    Components::Mesh& meshCmp = instanceCmp.mesh.Get();
                    Components::Material& materialCmp = instanceCmp.material.Get();
                    Components::Shader& shaderCmp = materialCmp.shader.Get();

                    Mesh* mesh = AssetLibrary::instance->Get<Mesh>(meshCmp.id);
                    if (!mesh)
                        continue;

                    Shader* shader = AssetLibrary::instance->Get<Shader>(shaderCmp.id);
                    if (!shader) 
                        continue;

                    state.flags |= Components::State::Flags::loaded;

                    uint materialIndex = viewWorld.materials.GetIndex(World::Entity(instanceCmp.material));

                    // everything should be loaded to be able to draw the instance

                    Components::WorldMatrix& matrixCmp = slot.Get<Components::WorldMatrix>();

                    float4x4 previousWorldMatrix = matrixCmp.matrix;
                    matrixCmp.matrix = ComputeWorldMatrix(ent);
                    previousWorldMatrix = matrixCmp.matrix;

                    HLSL::Instance instance;
                    instance.meshIndex = mesh->storageIndex;
                    instance.materialIndex = materialIndex;
                    instance.current = instance.pack(matrixCmp.matrix);
                    instance.previous = instance.pack(previousWorldMatrix);
                    instance.objectID = ent.ToUInt();
                    instance.rayTracingBLAS = mesh->BLAS.GetResource()->GetGPUVirtualAddress();

                    viewWorld.instances.AddOrUpdate(ent, instance);
                    localMeshletCount += mesh->LODs[0].meshletCount;

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

        uint queryIndex = world.Query(Components::Material::mask, 0);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
#define UpdateMaterialsStepSize 1024
        ViewWorld& frameWorld = viewWorld;

        tf::Task task = subflow.for_each_index(uint(0), entityCount, UpdateMaterialsStepSize,
            [&world, &frameWorld, queryIndex](int i)
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

                        material.shaderIndex = 0; // not used ?
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
        uint queryIndex = world.Query(Components::Light::mask, 0);

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
                uint queryIndex = world.Query(Components::Light::mask, 0);

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
                uint queryIndex = world.Query(Components::Camera::mask, 0);

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
                    hlslcam.viewProj_inv = inverse(hlslcam.viewProj);
                    hlslcam.planes[0] = planes[0];
                    hlslcam.planes[1] = planes[1];
                    hlslcam.planes[2] = planes[2];
                    hlslcam.planes[3] = planes[3];
                    hlslcam.planes[4] = planes[4];
                    hlslcam.planes[5] = planes[5];
                    hlslcam.worldPos = worldPos;

                    hlslcam.previousViewProj = previousViewProj;
                    hlslcam.previousViewProj_inv = inverse(hlslcam.previousViewProj);
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
                    }
                }
                this->viewWorld.cameras.Get().Add(hlslcamPrevious);

            }
        ).name("Update cameras");

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
                this->viewContext.meshletsToCull.Resize(this->viewWorld.meshletsCount);
                this->viewContext.meshletsCulledArgs.Resize(this->viewWorld.meshletsCount);
                this->viewContext.meshletsCulledArgsSorted.Resize(this->viewWorld.meshletsCount);
                this->viewContext.meshletBuckets.Resize(this->viewWorld.meshletsCount);
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

class EditorView : View
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

    void Execute() override
    {
        ZoneScoped;
        editor.Execute();
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
    MainView mainView;
    EditorView editorView;

    void On(uint2 _displayResolution)
    {
        ZoneScoped;
        instance = this;

        constantBuffer.On();
        meshStorage.On();
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
        constantBuffer.Off();
        instance = nullptr;
    }

    void Schedule(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        Profiler::instance->frameData.instancesCount = mainView.viewWorld.instances.Size();
        Profiler::instance->frameData.meshletsCount = mainView.viewWorld.meshletsCount;
        Profiler::instance->frameData.verticesCount = 0;

        constantBuffer.Reset();

        auto mainViewEndTask = mainView.Schedule(world, subflow);
        auto editorViewEndTask = editorView.Schedule(world, subflow);

        SUBTASKRENDERER(ExecuteFrame);
        SUBTASKRENDERER(WaitFrame);
        SUBTASKRENDERER(PresentFrame);

#define INTERLEAVEFRAMES
#ifdef INTERLEAVEFRAMES
        ExecuteFrame.succeed(mainViewEndTask, editorViewEndTask);
        ExecuteFrame.precede(WaitFrame);
        WaitFrame.precede(PresentFrame);
#else
        ExecuteFrame.succeed(mainViewEndTask, editorViewEndTask);
        WaitFrame.precede(ExecuteFrame);
        ExecuteFrame.precede(PresentFrame);
#endif
    }

    void ExecuteFrame()
    {
        ZoneScoped;

        AssetLibrary::instance->Close();
        AssetLibrary::instance->Execute();
        mainView.Execute();
        editorView.Execute();
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

        Resource::ReleaseResources();

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

};
Renderer* Renderer::instance;
