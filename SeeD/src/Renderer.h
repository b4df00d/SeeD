#pragma once

#include <map>
#include "Shaders/structs.hlsl"

#include "ffx_api/ffx_api.hpp"
#include "ffx_api/dx12/ffx_api_dx12.hpp"
#include "FidelityFX/host/backends/dx12/ffx_dx12.h"
#include "FidelityFX/host/ffx_spd.h"

// https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf

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


        //ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.67f, 0.67f, 0.67f, 0.39f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.08f, 0.50f, 0.72f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.67f, 0.67f, 0.67f, 0.39f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.67f, 0.67f, 0.67f, 0.39f);
        style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.41f, 0.42f, 0.44f, 1.00f);
        style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.29f, 0.30f, 0.31f, 0.67f);
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.08f, 0.08f, 0.09f, 0.83f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.33f, 0.34f, 0.36f, 0.83f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.23f, 0.23f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
        style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
        style.GrabRounding = style.FrameRounding = 2.3f;
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

};
UI* UI::instance;


static constexpr uint invalidMapIndex = UINT_MAX;
// is this is thread safe only because we allocate the max number of stuff we will find before the MT part
// with the reserve in the lock this should now be ok ?
template<typename keyType, typename gpuType>
class Map
{
    std::unordered_map<keyType, uint> keys;
    StructuredUploadBuffer<gpuType> gpuData;
public:
    std::atomic_uint32_t count = 0;
    std::recursive_mutex lock;

    Map()
    {
        keys.reserve(262144);
    }

    void Release()
    {
        gpuData.Release();
    }

    bool Contains(keyType key, uint& index)
    {
        if (keys.contains(key))
        {
            index = keys[key];
            return true;
        }
        return false;
    }
    bool Add(keyType key, uint& index)
    {
        if (!Contains(key, index))
        {
            // Adding without lock it baaaaadd !
            lock.lock();
            if (!Contains(key, index))
            {
                index = count++;
                keys[key] = index;
                Reserve(count);
            }
            lock.unlock();
            return true;
        }
        return false;
    }
    void Reserve(uint size)
    {
        if (gpuData.Size() < size)
        {
            lock.lock();
            gpuData.Resize(size);
            lock.unlock();
        }
    }
    gpuType& GetGPUData(uint index)
    {
        return gpuData[index];
    }
    gpuType& GetGPUData(keyType key)
    {
        uint index;
        bool present = Contains(key, index);
        seedAssert(present);
        return GetGPUData(index);
    }
    Resource& GetResource()
    {
        return gpuData.GetResource();
    }
    uint Size()
    {
        return (uint)gpuData.Size();
    }
    void Upload()
    {
        gpuData.Upload();
    }
    auto begin() { return keys.begin(); }
    auto end() { return keys.end(); }
};

// life time : frame
struct ViewWorld
{
    HLSL::CommonResourcesIndices commonResourcesIndices;
    StructuredUploadBuffer<HLSL::Camera> cameras;
    StructuredUploadBuffer<HLSL::Light> lights;
    Map<World::Entity, HLSL::Material> materials;
    StructuredUploadBuffer<HLSL::Instance> instances;
    StructuredBuffer<HLSL::Instance> instancesGPU; // only for instances created on GPU

    std::atomic<uint> meshletsCount;

    void Release()
    {
        cameras.Release();
        lights.Release();
        materials.Release();
        instances.Release();
        instancesGPU.Release();
    }
};

// life time : view (only updated on GPU)
struct CullingContext
{
    HLSL::CullingContext cullingContext; // to bind to rootSig

    StructuredBuffer<HLSL::Camera> camera;
    StructuredBuffer<HLSL::Light> lights;
    StructuredBuffer<HLSL::InstanceCullingDispatch> instancesInView;
    StructuredBuffer<HLSL::MeshletDrawCall> meshletsInView;
    StructuredBuffer<uint> instancesCounter;
    StructuredBuffer<uint> meshletsCounter;

    void On()
    {
        instancesInView.CreateBuffer(100, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        meshletsInView.CreateBuffer(1000, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        instancesCounter.CreateBuffer(1, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        meshletsCounter.CreateBuffer(1, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    }

    void Off()
    {
        camera.Release();
        lights.Release();
        instancesInView.Release();
        meshletsInView.Release();
        instancesCounter.Release();
        meshletsCounter.Release();
    }
};

struct ProbeGrid
{
    Resource probes;
    uint3 probesResolution;
    float3 probesBBSize;

    void On(uint3 res, float3 size)
    {
        probesResolution = res;
        probesBBSize = size;
        uint bufferCount = probesResolution.x * probesResolution.y * probesResolution.z;
        probes.CreateBuffer<HLSL::ProbeData>(bufferCount, "probes");
    }

    void Off()
    {
        probes.Release();
    }
};

struct RayTracingContext
{
    HLSL::RTParameters rtParameters;
    PerFrame<StructuredUploadBuffer<D3D12_RAYTRACING_INSTANCE_DESC>> instancesRayTracing;
    Resource TLAS;
    Resource GI;
    Resource shadows;
    PerFrame<Resource> giReservoir;
    ProbeGrid probes[3];

    void On(uint2 resolution)
    {
        TLAS.CreateAccelerationStructure(1000000, "TLAS");
        //scratchBuffer.CreateBuffer(1000000, 1, false, "scratchBuffer");
        GI.CreateTexture(resolution, DXGI_FORMAT_R11G11B10_FLOAT, false, "GI");
        shadows.CreateTexture(resolution, DXGI_FORMAT_R8_UNORM, false, "shadows");

        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            giReservoir.Get(i).CreateBuffer<HLSL::GIReservoirCompressed>(resolution.x * resolution.y, "GIReservoir");
        }

        float multi = 2;
        probes[0].On(uint3(8, 8, 8) * multi, float3(16, 16, 16) * multi);
        probes[1].On(uint3(8, 8, 8) * multi, float3(32, 32, 32) * multi);
        probes[2].On(uint3(8, 8, 8) * multi, float3(128, 128, 128) * multi);
    }

    void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            instancesRayTracing.Get(i).Release();
        }
        TLAS.Release();
        //scratchBuffer.Release();
        GI.Release();
        shadows.Release();

        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            giReservoir.Get(i).Release();
        }

        probes[0].Off();
        probes[1].Off();
        probes[2].Off();
    }

    void Reset()
    {
        instancesRayTracing->Clear();
    }
};

class View
{
public:
    uint frame;
    uint2 resolution;
    PerFrame<ViewWorld> viewWorld;
    RayTracingContext raytracingContext;
    CullingContext cullingContext;
    std::map<UINT64, Resource> resources;

    virtual void On(IOs::WindowInformation& window)
    {
        resolution = window.windowResolution;
        frame = 0;
        raytracingContext.On(resolution);
        cullingContext.On();
    }
    virtual void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            viewWorld.Get(i).Release();
        }
        raytracingContext.Off();
        cullingContext.Off();

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
    
    HLSL::CommonResourcesIndices SetupViewParams()
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
        commonResourcesIndices.camerasHeapIndex = viewWorld->cameras.gpuData.srv.offset;
        commonResourcesIndices.cameraCount = viewWorld->cameras.Size();
        commonResourcesIndices.lightsHeapIndex = viewWorld->lights.gpuData.srv.offset;
        commonResourcesIndices.lightCount = viewWorld->lights.Size();
        commonResourcesIndices.materialsHeapIndex = viewWorld->materials.GetResource().srv.offset;
        commonResourcesIndices.materialCount = viewWorld->materials.Size();
        commonResourcesIndices.instancesHeapIndex = viewWorld->instances.gpuData.srv.offset;
        commonResourcesIndices.instanceCount = viewWorld->instances.Size();
        commonResourcesIndices.instancesGPUHeapIndex = viewWorld->instancesGPU.gpuData.srv.offset;
        commonResourcesIndices.instanceGPUCount = viewWorld->instancesGPU.Size();

        return commonResourcesIndices;
    }
    
    HLSL::CullingContext SetupCullingContextParams()
    {
        HLSL::CullingContext cullingContextParams;

        cullingContextParams.reverseZ = true;
        cullingContextParams.frameNumber++;
        cullingContextParams.frameTime = Time::instance->currentTicks;
        cullingContextParams.cameraIndex = options.stopFrustumUpdate ? 1 : 0;
        cullingContextParams.lightsIndex = 0;
        cullingContextParams.culledInstanceIndex = cullingContext.instancesInView.GetResource().uav.offset;
        cullingContextParams.culledMeshletsIndex = cullingContext.meshletsInView.GetResource().uav.offset;
        cullingContextParams.instancesCounterIndex = cullingContext.instancesCounter.GetResource().uav.offset;
        cullingContextParams.meshletsCounterIndex = cullingContext.meshletsCounter.GetResource().uav.offset;
        cullingContextParams.albedoIndex = GetRegisteredResource("albedo").srv.offset;
        cullingContextParams.metalnessIndex = GetRegisteredResource("metalness").srv.offset;
        cullingContextParams.roughnessIndex = GetRegisteredResource("roughness").srv.offset;
        cullingContextParams.normalIndex = GetRegisteredResource("normal").srv.offset;
        cullingContextParams.motionIndex = GetRegisteredResource("motion").srv.offset;
        cullingContextParams.depthIndex = GetRegisteredResource("depth").srv.offset;
        cullingContextParams.HZB = GetRegisteredResource("depthDownSample").srv.offset;
        cullingContextParams.resolution = float4(float(resolution.x), float(resolution.y), 1.0f / resolution.x, 1.0f / resolution.y);
        cullingContextParams.HZBMipCount = GetRegisteredResource("depthDownSample").GetResource()->GetDesc().MipLevels;

        return cullingContextParams;
    }

    HLSL::RTParameters SetupRayTracingContextParams()
    {
        HLSL::RTParameters rayTracingContextParams;

        rayTracingContextParams.frame = frame++;
        if (IOs::instance->keys.pressed[VK_R])
            rayTracingContextParams.frame = 0;
        rayTracingContextParams.BVH = raytracingContext.TLAS.srv.offset;
        rayTracingContextParams.giIndex = raytracingContext.GI.uav.offset;
        rayTracingContextParams.shadowsIndex = raytracingContext.shadows.uav.offset;
        rayTracingContextParams.resolution = float4(1.0f * resolution.x, 1.0f * resolution.y, 1.0f / resolution.x, 1.0f / resolution.y);
        rayTracingContextParams.giReservoirIndex = raytracingContext.giReservoir.Get().uav.offset;
        rayTracingContextParams.previousgiReservoirIndex = raytracingContext.giReservoir.GetPrevious().uav.offset;
        rayTracingContextParams.passNumber = 0;
        //rayTracingContextParams.lightedIndex = lighted.Get().uav.offset;

        for (uint i = 0; i < ARRAYSIZE(raytracingContext.probes); i++)
        {
            rayTracingContextParams.probes[i].probesSamplesPerFrame = 2 * (3 - i);
            rayTracingContextParams.probes[i].probesIndex = raytracingContext.probes[i].probes.uav.offset;
            rayTracingContextParams.probes[i].probesResolution = uint4(raytracingContext.probes[i].probesResolution, 0);
            float3 probeCellSize = (raytracingContext.probes[i].probesBBSize / float3(raytracingContext.probes[i].probesResolution));
            float3 camWorldPos = viewWorld->cameras[0].worldPos.xyz + inverse(viewWorld->cameras[0].view)[2].xyz * raytracingContext.probes[i].probesBBSize * 0.33f;
            int3 camCellPos = floor(camWorldPos / probeCellSize);
            camWorldPos = float3(camCellPos) * probeCellSize;
            rayTracingContextParams.probes[i].probesBBMin = float4(camWorldPos.xyz - raytracingContext.probes[i].probesBBSize * 0.5f, 0);
            rayTracingContextParams.probes[i].probesBBMax = float4(camWorldPos.xyz + raytracingContext.probes[i].probesBBSize * 0.5f, 0);
             rayTracingContextParams.probes[i].probesAddressOffset = uint4(ModulusI(camCellPos, int3(raytracingContext.probes[i].probesResolution)), 0);
        }

        return rayTracingContextParams;
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
    View* view;
public:
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

    void SetupView(View* view, Resource* RT, uint RTCount, bool clearRT, Resource* depth, bool clearDepth)
    {
        ZoneScoped;
        UINT64 w = view->resolution.x;
        UINT64 h = view->resolution.y;

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
            commandBuffer->cmd->ClearRenderTargetView(RTs[i], clearColor.f32, 1, &rect);
        }

        commandBuffer->cmd->OMSetRenderTargets(RTCount, RTs, true, depth ? &depth->dsv.handle : nullptr);

        if (depth)
        {
            float clearDepthValue(1.0f);
            if (HLSL::reverseZ) clearDepthValue = 0;
            UINT8 clearStencilValue(0);
            commandBuffer->cmd->ClearDepthStencilView(depth->dsv.handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clearDepthValue, clearStencilValue, 1, &rect);
        }
    }

    virtual void Setup(View* view) = 0;
    virtual void Render(View* view) = 0;
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

        bool allowUpdate = true;

        //CPU stuff ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

        // The generated AS can support iterative updates. This may change the final
        // size of the AS as well as the temporary memory requirements, and hence has
        // to be set before the actual build
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = allowUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
            : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

        // Describe the work being requested, in this case the construction of a
        // (possibly dynamic) top-level hierarchy, with the given instance descriptors
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
            prebuildDesc = {};
        prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        prebuildDesc.NumDescs = static_cast<UINT>(view->viewWorld->instances.Size());
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
        uint descriptorsSizeInBytes = ROUND_UP(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * static_cast<UINT64>(view->viewWorld->instances.Size()), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);


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

        // Create a descriptor of the requested builder work, to generate a top-level
        // AS from the input parameters
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        buildDesc.Inputs.InstanceDescs = view->raytracingContext.instancesRayTracing->GetGPUVirtualAddress();
        buildDesc.Inputs.NumDescs = view->raytracingContext.instancesRayTracing->Size();
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
        depthDownSample.Get().CreateTexture(view->resolution, DXGI_FORMAT_R32_FLOAT, true, "depthDownSample");

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
    Components::Handle<Components::Shader> cullingMeshletsShader;
public:
    virtual void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        depth.Register("depth", view);
        cullingResetShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\cullingReset.hlsl");
        cullingInstancesShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\cullingInstances.hlsl");
        cullingMeshletsShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\cullingMeshlets.hlsl");
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        view->cullingContext.instancesInView.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
        view->cullingContext.meshletsInView.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
        view->cullingContext.instancesCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
        view->cullingContext.meshletsCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);

        auto& instances = view->viewWorld->instances;
        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld->commonResourcesIndices);
        auto cullingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->cullingContext.cullingContext);

        Shader& reset = *AssetLibrary::instance->Get<Shader>(cullingResetShader.Get().id, true);
        commandBuffer->SetCompute(reset);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, cullingContextAddress);
        commandBuffer->cmd->Dispatch(1, 1, 1);

        Shader& cullingInstances = *AssetLibrary::instance->Get<Shader>(cullingInstancesShader.Get().id, true);
        commandBuffer->SetCompute(cullingInstances);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, cullingContextAddress);
        commandBuffer->cmd->Dispatch(cullingInstances.DispatchX(instances.Size()), 1, 1);

        view->cullingContext.instancesInView.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        view->cullingContext.instancesCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        Shader& cullingMeshlets = *AssetLibrary::instance->Get<Shader>(cullingMeshletsShader.Get().id, true);
        commandBuffer->SetCompute(cullingMeshlets);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, cullingContextAddress);
        commandBuffer->cmd->ExecuteIndirect(cullingMeshlets.commandSignature, instances.Size(), view->cullingContext.instancesInView.GetResourcePtr(), 0, view->cullingContext.instancesCounter.GetResourcePtr(), 0);

        view->cullingContext.meshletsInView.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        view->cullingContext.meshletsCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

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
        depth.Get().CreateDepthTarget(view->resolution, "depth");
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
    ViewResource normal;
    ViewResource metalness;
    ViewResource roughness;
    ViewResource depth;
    ViewResource motion;
    Components::Handle<Components::Shader> meshShader;
public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        albedo.Register("albedo", view);
        albedo.Get().CreateRenderTarget(view->resolution, DXGI_FORMAT_R8G8B8A8_UNORM, "albedo"); // must be same as backbuffer for a resource copy at end of frame 
        normal.Register("normal", view);
        normal.Get().CreateRenderTarget(view->resolution, DXGI_FORMAT_R11G11B10_FLOAT, "normal");
        metalness.Register("metalness", view);
        metalness.Get().CreateRenderTarget(view->resolution, DXGI_FORMAT_R8_UNORM, "metalness");
        roughness.Register("roughness", view);
        roughness.Get().CreateRenderTarget(view->resolution, DXGI_FORMAT_R8_UNORM, "roughness");
        depth.Register("depth", view);
        motion.Register("motion", view);
        motion.Get().CreateRenderTarget(view->resolution, DXGI_FORMAT_R16G16_FLOAT, "motion");
        meshShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\mesh.hlsl");
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
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        roughness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);

        Resource rts[] = { albedo.Get(), normal.Get(), metalness.Get(), roughness.Get(), motion.Get() };
        SetupView(view, rts, ARRAYSIZE(rts), true, &depth.Get(), true);

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld->commonResourcesIndices);
        auto cullingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->cullingContext.cullingContext);

        Shader& shader = *AssetLibrary::instance->Get<Shader>(meshShader.Get().id, true);
        commandBuffer->SetGraphic(shader);

        commandBuffer->cmd->SetGraphicsRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetGraphicsRootConstantBufferView(1, cullingContextAddress);

        uint maxDraw = view->cullingContext.meshletsInView.Size();
        commandBuffer->cmd->ExecuteIndirect(shader.commandSignature, maxDraw, view->cullingContext.meshletsInView.GetResourcePtr(), 0, view->cullingContext.meshletsCounter.GetResourcePtr(), 0);

        albedo.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        normal.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        metalness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        roughness.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        motion.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

        Close();
    }
};

class LightingProbes : public Pass
{
    Components::Handle<Components::Shader> rayProbesDispatchShader;

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        rayProbesDispatchShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\raytracingprobes.hlsl");
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld->commonResourcesIndices);
        auto cullingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->cullingContext.cullingContext);

        Shader& rayDispatch = *AssetLibrary::instance->Get<Shader>(rayProbesDispatchShader.Get().id, true);
        commandBuffer->SetRaytracing(rayDispatch);
        // global root sig for ray tracing is the same as compute shaders
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, cullingContextAddress);

        for (uint i = 0; i < ARRAYSIZE(view->raytracingContext.probes); i++)
        {
            view->raytracingContext.rtParameters.probeToCompute = i;
            auto raytracingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);
            commandBuffer->cmd->SetComputeRootConstantBufferView(2, raytracingContextAddress);

            D3D12_DISPATCH_RAYS_DESC drd = rayDispatch.GetRTDesc();
            drd.Width = view->raytracingContext.probes[i].probesResolution.x;
            drd.Height = view->raytracingContext.probes[i].probesResolution.y;
            drd.Depth = view->raytracingContext.probes[i].probesResolution.z;

            commandBuffer->cmd->DispatchRays(&drd);
        }

        view->raytracingContext.GI.Barrier(commandBuffer.Get());

        Close();
    }
};

class Lighting : public Pass
{
    ViewResource lighted;
    ViewResource albedo;
    ViewResource depth;
    ViewResource normal;
    Components::Handle<Components::Shader> rayDispatchShader;
    Components::Handle<Components::Shader> ReSTIRSpacialShader;
    Components::Handle<Components::Shader> applyLightingShader;

public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        lighted.Register("lighted", view);
        lighted.Get().CreateRenderTarget(view->resolution, DXGI_FORMAT_R10G10B10A2_UNORM, "lighted");
        albedo.Register("albedo", view);
        depth.Register("depth", view);
        normal.Register("normal", view);
        rayDispatchShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\raytracing.hlsl");
        ReSTIRSpacialShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\ReSTIRSpacial.hlsl");
        applyLightingShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\lighting.hlsl");
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();


        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON);

        view->raytracingContext.rtParameters.lightedIndex = lighted.Get().uav.offset;

        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld->commonResourcesIndices);
        auto cullingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->cullingContext.cullingContext);
        auto raytracingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);

        // Trace rays (+ temporal ReSTIR)
        Shader& rayDispatch = *AssetLibrary::instance->Get<Shader>(rayDispatchShader.Get().id, true);
        commandBuffer->SetRaytracing(rayDispatch); 
        // global root sig for ray tracing is the same as compute shaders
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, cullingContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(2, raytracingContextAddress);

        D3D12_DISPATCH_RAYS_DESC drd = rayDispatch.GetRTDesc();
        drd.Width = view->resolution.x;
        drd.Height = view->resolution.y;

        commandBuffer->cmd->DispatchRays(&drd);

        // Spacial ReSTIR pass 1
        view->raytracingContext.giReservoir.Get().Barrier(commandBuffer.Get());
        Shader& ReSTIRSpacial = *AssetLibrary::instance->Get<Shader>(ReSTIRSpacialShader.Get().id, true);
        commandBuffer->SetCompute(ReSTIRSpacial);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, cullingContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(2, raytracingContextAddress);
        commandBuffer->cmd->Dispatch(ReSTIRSpacial.DispatchX(view->resolution.x), ReSTIRSpacial.DispatchY(view->resolution.y), 1);

        // Spacial ReSTIR pass 2
        view->raytracingContext.giReservoir.Get().Barrier(commandBuffer.Get());
        uint tmp = view->raytracingContext.rtParameters.giReservoirIndex;
        view->raytracingContext.rtParameters.giReservoirIndex = view->raytracingContext.rtParameters.previousgiReservoirIndex;
        view->raytracingContext.rtParameters.previousgiReservoirIndex = tmp;
        //std::swap(view->raytracingContext.rtParameters.giReservoirIndex, view->raytracingContext.rtParameters.previousgiReservoirIndex);
        view->raytracingContext.rtParameters.passNumber = 1;
        auto raytracingContextAddress2 = ConstantBuffer::instance->PushConstantBuffer(&view->raytracingContext.rtParameters);
        commandBuffer->cmd->SetComputeRootConstantBufferView(2, raytracingContextAddress2);
        commandBuffer->cmd->Dispatch(ReSTIRSpacial.DispatchX(view->resolution.x), ReSTIRSpacial.DispatchY(view->resolution.y), 1);
        //std::swap(view->raytracingContext.rtParameters.giReservoirIndex, view->raytracingContext.rtParameters.previousgiReservoirIndex); // reverse again if somebody need the original latter ?

        // Lighting
        view->raytracingContext.GI.Barrier(commandBuffer.Get());

        Shader& applyLighting = *AssetLibrary::instance->Get<Shader>(applyLightingShader.Get().id, true);
        commandBuffer->SetCompute(applyLighting);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, cullingContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(2, raytracingContextAddress);
        commandBuffer->cmd->Dispatch(applyLighting.DispatchX(view->resolution.x), applyLighting.DispatchY(view->resolution.y), 1);

        lighted.Get().Barrier(commandBuffer.Get());


        depth.Get().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        Close();
    }
};

class Forward : public Pass
{
    ViewResource depth;
    Components::Handle<Components::Shader> meshShader;
public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        depth.Register("depth", view);
        meshShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\mesh.hlsl");
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

class PostProcess : public Pass
{
    ViewResource lighted;
    ViewResource albedo;
    ViewResource normal;
    ViewResource motion;
    ViewResource depth;
    Components::Handle<Components::Shader> postProcessShader;

public:
    HLSL::PostProcessParameters ppparams;

    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        lighted.Register("lighted", view);
        albedo.Register("albedo", view);
        normal.Register("normal", view);
        motion.Register("motion", view);
        depth.Register("depth", view);
        postProcessShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\PostProcess.hlsl");

        ppparams.P = 1;
        ppparams.a = 1;
        ppparams.m = 0.22;
        ppparams.l = 0.4;
        ppparams.c = 1.33;
        ppparams.b = 0.0;
        ppparams.expoAdd = 0;
        ppparams.expoMul = 1;
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

        ppparams.resolution = float4(1.0f * view->resolution.x, 1.0f * view->resolution.y, 1.0f / view->resolution.x, 1.0f / view->resolution.y);
        ppparams.lightedIndex = lighted.Get().uav.offset;
        ppparams.backBufferIndex = GPU::instance->backBuffer.Get().uav.offset;


        auto commonResourcesIndicesAddress = ConstantBuffer::instance->PushConstantBuffer(&view->viewWorld->commonResourcesIndices);
        auto cullingContextAddress = ConstantBuffer::instance->PushConstantBuffer(&view->cullingContext.cullingContext);
        auto postProcessParametersAddress = ConstantBuffer::instance->PushConstantBuffer(&ppparams);

        Shader& postProcess = *AssetLibrary::instance->Get<Shader>(postProcessShader.Get().id, true);
        commandBuffer->SetCompute(postProcess);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, commonResourcesIndicesAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, cullingContextAddress);
        commandBuffer->cmd->SetComputeRootConstantBufferView(2, postProcessParametersAddress);
        commandBuffer->cmd->Dispatch(postProcess.DispatchX(view->resolution.x), postProcess.DispatchY(view->resolution.y), 1);

        Close();
    }
};

class Present : public Pass
{
    ViewResource albedo;
    ViewResource depth;
public:
    void On(View* view, ID3D12CommandQueue* queue, String _name, PerFrame<CommandBuffer>* _dependency, PerFrame<CommandBuffer>* _dependency2) override
    {
        Pass::On(view, queue, _name, _dependency, _dependency2);
        ZoneScoped;
        albedo.Register("albedo", view);
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
        commandBuffer->cmd->CopyResource(GPU::instance->backBuffer.Get().GetResource(), albedo.Get().GetResource());
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
    HZB hzb;
    Skinning skinning;
    Particles particles;
    Spawning spawning;
    AccelerationStructure accelerationStructure;
    Culling culling;
    ZPrepass zPrepass;
    GBuffers gBuffers;
    LightingProbes lightingProbes;
    Lighting lighting;
    Forward forward;
    PostProcess postProcess;
    Present present;


    void On(IOs::WindowInformation& window) override
    {
        View::On(window);

        hzb.On(this, GPU::instance->graphicQueue, "hzb", nullptr, nullptr);
        skinning.On(this, GPU::instance->computeQueue, "skinning", &AssetLibrary::instance->commandBuffer, nullptr);
        particles.On(this, GPU::instance->computeQueue, "particles", &AssetLibrary::instance->commandBuffer, nullptr);
        spawning.On(this, GPU::instance->computeQueue, "spawning", &AssetLibrary::instance->commandBuffer, nullptr);
        accelerationStructure.On(this, GPU::instance->computeQueue, "accelerationStructure", &AssetLibrary::instance->commandBuffer, nullptr);
        culling.On(this, GPU::instance->graphicQueue, "culling", &hzb.commandBuffer, nullptr);
        zPrepass.On(this, GPU::instance->graphicQueue, "zPrepass", &culling.commandBuffer, nullptr);
        gBuffers.On(this, GPU::instance->graphicQueue, "gBuffers", &zPrepass.commandBuffer, nullptr);
        lightingProbes.On(this, GPU::instance->computeQueue, "lightingProbes", &accelerationStructure.commandBuffer, nullptr);
        lighting.On(this, GPU::instance->graphicQueue, "lighting", &accelerationStructure.commandBuffer, &lightingProbes.commandBuffer);
        forward.On(this, GPU::instance->graphicQueue, "forward", &lighting.commandBuffer, nullptr);
        postProcess.On(this, GPU::instance->graphicQueue, "postProcess", &forward.commandBuffer, nullptr);
        present.On(this, GPU::instance->graphicQueue, "present", &postProcess.commandBuffer, nullptr);
    }

    void Off() override
    {
        hzb.Off();
        skinning.Off();
        particles.Off();
        spawning.Off();
        accelerationStructure.Off();
        culling.Off();
        zPrepass.Off();
        gBuffers.Off();
        lightingProbes.Off();
        lighting.Off();
        forward.Off();
        postProcess.Off();
        present.Off();

        View::Off();
    }

    tf::Task Schedule(World& world, tf::Subflow& subflow) override
    {
        ZoneScoped;

        tf::Task reset = Reset(world, subflow);
        tf::Task updateInstances = UpdateInstances(world, subflow);
        tf::Task updateMaterials = UpdateMaterials(world, subflow);
        tf::Task updateLights = UpdateLights(world, subflow);
        tf::Task updateCameras = UpdateCameras(world, subflow);

        tf::Task uploadAndSetup = UploadAndSetup(world, subflow);

        SUBTASKVIEWPASS(hzb);
        SUBTASKVIEWPASS(skinning);
        SUBTASKVIEWPASS(particles);
        SUBTASKVIEWPASS(spawning);
        SUBTASKVIEWPASS(accelerationStructure);
        SUBTASKVIEWPASS(culling);
        SUBTASKVIEWPASS(zPrepass);
        SUBTASKVIEWPASS(gBuffers);
        SUBTASKVIEWPASS(lightingProbes);
        SUBTASKVIEWPASS(lighting);
        SUBTASKVIEWPASS(forward);
        SUBTASKVIEWPASS(postProcess);
        SUBTASKVIEWPASS(present);

        reset.precede(updateInstances, updateMaterials, updateLights, updateCameras); // should precede all, user need to check that

        updateInstances.precede(updateMaterials);
        updateMaterials.precede(uploadAndSetup);
        updateCameras.precede(uploadAndSetup);
        updateLights.precede(uploadAndSetup);
        uploadAndSetup.precede(skinningTask, particlesTask, spawningTask, accelerationStructureTask, cullingTask, zPrepassTask, gBuffersTask, lightingProbesTask, lightingTask, forwardTask, postProcessTask);

        presentTask.succeed(uploadAndSetup, hzbTask, skinningTask, particlesTask, spawningTask, accelerationStructureTask, cullingTask, zPrepassTask, gBuffersTask, lightingProbesTask, lightingTask, forwardTask, postProcessTask);

        return presentTask;
    }

    void Execute() override
    {
        ZoneScoped;
        hzb.Execute();
        skinning.Execute();
        particles.Execute();
        spawning.Execute();
        accelerationStructure.Execute();
        culling.Execute();
        zPrepass.Execute();
        gBuffers.Execute();
        lightingProbes.Execute();
        lighting.Execute();
        forward.Execute();
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
                viewWorld->instances.Clear();
                viewWorld->meshletsCount = 0;
                viewWorld->lights.Clear();
                viewWorld->cameras.Clear();
                raytracingContext.instancesRayTracing->Clear();
            }
        ).name("Reset");
        return task;
    }

    tf::Task UpdateInstances(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

#define UpdateInstancesStepSize 128
        ViewWorld& frameWorld = viewWorld.Get();
        
        uint instanceQueryIndex = world.Query(Components::Instance::mask | Components::WorldMatrix::mask, 0);
        uint entityCount = (uint)world.frameQueries[instanceQueryIndex].size();
        frameWorld.instances.Reserve(entityCount);

        uint materialsCount = world.CountQuery(Components::Material::mask, 0);
        frameWorld.materials.Reserve(materialsCount);
        
        tf::Task task = subflow.for_each_index(0, entityCount, UpdateInstancesStepSize,
            [this, &world, instanceQueryIndex](int i)
            {
                ZoneScopedN("UpdateInstance");

                std::array<HLSL::Instance, UpdateInstancesStepSize> localInstances;
                std::array<D3D12_RAYTRACING_INSTANCE_DESC, UpdateInstancesStepSize> localInstancesRayTracing;
                uint localMeshletCount = 0;
                uint instanceCount = 0;
                uint instanceRayTracingCount = 0;
                for (uint subQuery = 0; subQuery < UpdateInstancesStepSize; subQuery++)
                {
                    auto& queryResult = world.frameQueries[instanceQueryIndex];
                    if ((i + subQuery) > (queryResult.size() - 1)) 
                        break;

                    auto& slot = queryResult[i + subQuery];
                    World::Entity ent = World::Entity(slot.Get<Components::Entity>().index);

                    Components::Instance& instanceCmp = slot.Get<Components::Instance>();
                    Components::Mesh& meshCmp = instanceCmp.mesh.Get();
                    Components::Material& materialCmp = instanceCmp.material.Get();
                    Components::Shader& shaderCmp = materialCmp.shader.Get();

                    uint meshIndex = AssetLibrary::instance->GetIndex(meshCmp.id);
                    if (meshIndex == ~0)
                        continue;

                    Shader* shader = AssetLibrary::instance->Get<Shader>(shaderCmp.id);
                    if (!shader) 
                        continue;

                    uint materialIndex;
                    bool materialAdded = viewWorld->materials.Add(World::Entity(instanceCmp.material.index), materialIndex);

                    // everything should be loaded to be able to draw the instance

                    Components::WorldMatrix& matrixCmp = slot.Get<Components::WorldMatrix>();
                    float4x4 previousWorldMatrix = matrixCmp.matrix;
                    float4x4 worldMatrix = ComputeWorldMatrix(ent);
                    //worldMatrix[3][1] = sin(worldMatrix[3][2] + 1.0f * Time::instance->currentTicks * 0.000001f);
                    matrixCmp.matrix = worldMatrix;

                    HLSL::Instance& instance = localInstances[instanceCount];
                    instance.meshIndex = meshIndex;
                    instance.materialIndex = materialIndex;
                    //instance.worldMatrix = worldMatrix;
                    instance.current = instance.pack(worldMatrix);
                    instance.previous = instance.pack(previousWorldMatrix);
                    // count instances with shader
                    instanceCount++;

                    Mesh* mesh = AssetLibrary::instance->Get<Mesh>(meshCmp.id);
                    localMeshletCount += mesh->meshletCount;


                    // if in range (depending on distance and BC size)
                        // Add to TLAS
                    D3D12_RAYTRACING_INSTANCE_DESC& instanceDesc = localInstancesRayTracing[instanceRayTracingCount];
                    // Instance ID visible in the shader in InstanceID()
                    instanceDesc.InstanceID = instanceRayTracingCount;
                    // Index of the hit group invoked upon intersection
                    instanceDesc.InstanceContributionToHitGroupIndex = 0;
                    // Instance flags, including backface culling, winding, etc - TODO: should be accessible from outside
                    instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE | D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
                    // Instance transform matrix
                    worldMatrix = transpose(worldMatrix);
                    memcpy(instanceDesc.Transform, &worldMatrix, sizeof(instanceDesc.Transform));
                    // Get access to the bottom level
                    instanceDesc.AccelerationStructure = mesh->BLAS.GetResource()->GetGPUVirtualAddress();
                    // Visibility mask, always visible here - TODO: should be accessible from outside
                    instanceDesc.InstanceMask = 0xFF;

                    instanceRayTracingCount++;
                }

                this->InstanceRTSync.lock();
                viewWorld->instances.AddRange(localInstances.data(), instanceCount);
                viewWorld->meshletsCount += localMeshletCount;
                raytracingContext.instancesRayTracing->AddRangeWithTransform(localInstancesRayTracing.data(), instanceRayTracingCount, [](int index, D3D12_RAYTRACING_INSTANCE_DESC& data) { data.InstanceID = index; });
                // THIS IS WRONG : data.InstanceID should be equal to the instance index in viewWorld.instances : this is not guarantied
                // BUT ! I have InstanceRTSync now ...
                this->InstanceRTSync.unlock();
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
        ViewWorld& frameWorld = viewWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, UpdateMaterialsStepSize,
            [&world, &frameWorld, queryIndex](int i)
            {
                ZoneScopedN("UpdateMaterials");
                for (uint subQuery = 0; subQuery < UpdateMaterialsStepSize; subQuery++)
                {
                    auto& queryResult = world.frameQueries[queryIndex];
                    if (i + subQuery > queryResult.size() - 1) 
                        return;

                    uint materialIndex;
                    if (frameWorld.materials.Contains(World::Entity(queryResult[i + subQuery].Get<Components::Entity>().index), materialIndex))
                    {
                        Components::Material& materialCmp = queryResult[i + subQuery].Get<Components::Material>();
                        HLSL::Material& material = frameWorld.materials.GetGPUData(materialIndex);

                        material.shaderIndex = AssetLibrary::instance->GetIndex(materialCmp.shader.Get().id);
                        for (uint paramIndex = 0; paramIndex < HLSL::MaterialParametersCount; paramIndex++)
                        {
                            // memcpy ? it is even just a cashline 
                            material.parameters[paramIndex] = materialCmp.prameters[paramIndex];
                        }
                        bool materialReady = true;
                        for (uint texIndex = 0; texIndex < HLSL::MaterialTextureCount; texIndex++)
                        {
                            if (materialCmp.textures[texIndex].index != entityInvalid)
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
        ViewWorld& frameWorld = viewWorld.Get();

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
                ViewWorld& frameWorld = viewWorld.Get();
                auto& queryResult = world.frameQueries[queryIndex];

                for (uint i = 0; i < entityCount; i++)
                {
                    auto& light = queryResult[i].Get<Components::Light>();
                    //auto& trans = queryResult[i].Get<Components::WorldMatrix>();

                    World::Entity ent = queryResult[i].Get<Components::Entity>().index;
                    float4x4 worldMatrix = ComputeWorldMatrix(ent);

                    HLSL::Light hlsllight;

                    hlsllight.pos = float4(worldMatrix[3].xyz, 1);
                    hlsllight.dir = float4(normalize(worldMatrix[2].xyz), 1);
                    hlsllight.color = light.color;
                    hlsllight.angle = light.angle;
                    hlsllight.range = light.range;

                    this->viewWorld->lights.Add(hlsllight);
                }
                this->viewWorld->lights.Upload();
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
                ViewWorld& frameWorld = viewWorld.Get();
                auto& queryResult = world.frameQueries[queryIndex];

                static HLSL::Camera hlslcamPrevious = {};
                if (!options.stopFrustumUpdate)
                {
                    if (this->viewWorld.GetPrevious().cameras.Size() > 0)
                    {
                        hlslcamPrevious = this->viewWorld.GetPrevious().cameras[0];
                    }
                }

                for (uint i = 0; i < entityCount; i++)
                {
                    auto& cam = queryResult[i].Get<Components::Camera>();
                    auto& trans = queryResult[i].Get<Components::Transform>();
                    auto& mat = queryResult[i].Get<Components::WorldMatrix>();
                    float4x4 previousMat = mat.matrix;
                    mat.matrix = Matrix(trans.position, trans.rotation, trans.scale);

                    float4x4 proj = MatrixPerspectiveFovLH(cam.fovY * (3.14f / 180.0f), float(this->resolution.x) / float(this->resolution.y), cam.nearClip, cam.farClip, HLSL::reverseZ);
                    float4x4 viewProj = mul(inverse(mat.matrix), proj);
                    float4 worldPos = float4(mat.matrix[3].xyz, 1);
                    float4x4 previousViewProj = mul(inverse(previousMat), proj);
                    float4 previousWorldPos = float4(previousMat[3].xyz, 1);

                    float4 planes[6];
                    float3 worldCorners[8];
                    //float sizeCulling;

                    // compute planes
                    float4x4 matProj = mul(inverse(MatrixPerspectiveFovLH(cam.fovY * (3.14f / 180.0f), float(this->resolution.x) / float(this->resolution.y), cam.nearClip, cam.farClip, false)), mat.matrix);

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
                    hlslcam.proj = proj;
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

                    this->viewWorld->cameras.Add(hlslcam);

                    editorState.cameraView = mat.matrix;
                    editorState.cameraProj = hlslcam.proj;
                }
                this->viewWorld->cameras.Add(hlslcamPrevious);

                this->viewWorld->cameras.Upload();
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
                // put cameras and lights upload here too ?
                this->viewWorld->instances.Upload();
                this->viewWorld->materials.Upload();
                this->raytracingContext.instancesRayTracing->Upload();
                this->cullingContext.instancesInView.Resize(this->viewWorld->instances.Size());
                this->cullingContext.meshletsInView.Resize(this->viewWorld->meshletsCount);
                this->viewWorld->commonResourcesIndices = SetupViewParams();
                this->cullingContext.cullingContext = SetupCullingContextParams();
                this->raytracingContext.rtParameters = SetupRayTracingContextParams();
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

    void On(IOs::WindowInformation& window) override
    {
        resolution = window.windowResolution;

        editor.On(this, GPU::instance->graphicQueue, "editor", nullptr, nullptr);
    }

    void Off() override
    {
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

    void On(IOs::WindowInformation& window)
    {
        instance = this;
        constantBuffer.On();
        meshStorage.On();
        mainView.On(window);
        editorView.On(window);

        endOfLastFrame = &editorView.editor.commandBuffer;
    }
    
    void Off()
    {
        editorView.Off();
        mainView.Off();
        meshStorage.Off();
        constantBuffer.Off();
        instance = nullptr;
    }

    void Schedule(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        Profiler::instance->frameData.instancesCount = mainView.viewWorld->instances.Size();
        Profiler::instance->frameData.meshletsCount = mainView.viewWorld->meshletsCount;
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
        //HRESULT hr;

        AssetLibrary::instance->Close();
        AssetLibrary::instance->Execute();
        mainView.Execute();
        editorView.Execute();
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

        Resource::ReleaseResources();
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
