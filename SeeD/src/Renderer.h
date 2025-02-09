#pragma once

#include <map>
#include "Shaders/structs.hlsl"

#include "ffx_api/ffx_api.hpp"
#include "ffx_api/dx12/ffx_api_dx12.hpp"
#include "FidelityFX/host/backends/dx12/ffx_dx12.h"
#include "FidelityFX/host/ffx_spd.h"


#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

class UI
{
    ID3D12DescriptorHeap* pd3dSrvDescHeap = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE  hFontSrvCpuDescHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE  hFontSrvGpuDescHandle = {};
    ID3D12GraphicsCommandList* cmdList = nullptr;
    ID3D12CommandAllocator* cmdAlloc = nullptr;

public:
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

        ImGui_ImplDX12_Init(device, sc.BufferCount, sc.BufferDesc.Format, pd3dSrvDescHeap, hFontSrvCpuDescHandle, hFontSrvGpuDescHandle);
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
template<typename keyType, typename cpuType, typename gpuType>
class Map
{
    std::unordered_map<keyType, uint> keys;
    std::vector<cpuType> data;
    StructuredUploadBuffer<gpuType> gpuData;
    std::vector<bool> loaded; // and uploaded if applicable
public:
    std::atomic_uint32_t count = 0;
    std::recursive_mutex lock;
    uint maxLoading = 3;

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
                SetLoaded(index, false);
            }
            lock.unlock();
            return true;
        }
        return false;
    }
    void Reserve(uint size)
    {
        if (data.size() < size)
        {
            lock.lock();
            data.resize(size);
            gpuData.Resize(size);
            loaded.resize(size);
            lock.unlock();
        }
    }
    bool GetLoaded(uint index)
    {
        return loaded[index];
    }
    bool GetLoaded(keyType key)
    {
        uint index;
        bool present = Contains(key, index);
        seedAssert(present);
        return loaded[index];
    }
    void SetLoaded(uint index, bool value)
    {
        loaded[index] = value;
    }
    void SetLoaded(keyType key, bool value)
    {
        uint index;
        bool present = Contains(key, index);
        seedAssert(present);
        loaded[index] = value;
    }
    cpuType& GetData(uint index)
    {
        return data[index];
    }
    cpuType& GetData(keyType key)
    {
        uint index;
        bool present = Contains(key, index);
        seedAssert(present);
        return GetData(index);
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
        return data.size();
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
    StructuredUploadBuffer<HLSL::CommonResourcesIndices> commonResourcesIndices;
    StructuredUploadBuffer<HLSL::Camera> cameras;
    StructuredUploadBuffer<HLSL::Light> lights;
    Map<World::Entity, Material, HLSL::Material> materials;
    StructuredUploadBuffer<HLSL::Instance> instances;
    StructuredBuffer<HLSL::Instance> instancesGPU; // only for instances created on GPU
    TLAS tlas;
    std::atomic<uint> meshletsCount;

    void Release()
    {
        commonResourcesIndices.Release();
        cameras.Release();
        lights.Release();
        instances.Release();
        instancesGPU.Release();
        materials.Release();
    }
};

// life time : view (only updated on GPU)
struct CullingContext
{
    PerFrame<StructuredUploadBuffer<HLSL::CullingContext>> cullingContext; // to bind to rootSig

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

    void Release()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            cullingContext.Get(i).Release();
        }
        camera.Release();
        lights.Release();
        instancesInView.Release();
        meshletsInView.Release();
        instancesCounter.Release();
        meshletsCounter.Release();
    }
};

class View
{
public:
    uint2 resolution;
    PerFrame<ViewWorld> viewWorld;
    CullingContext cullingContext;
    std::map<UINT64, Resource> resources;

    virtual void On(IOs::WindowInformation& window)
    {
        cullingContext.On();
    }
    virtual void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            viewWorld.Get(i).Release();
        }
        cullingContext.Release();

        for (auto& item : resources)
        {
            item.second.Release();
        }
    }
    virtual tf::Task Schedule(World& world, tf::Subflow& subflow) = 0;
    virtual void Execute() = 0;
    void SetupViewParams()
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

        viewWorld->commonResourcesIndices.Clear();
        viewWorld->commonResourcesIndices.Add(commonResourcesIndices);
        viewWorld->commonResourcesIndices.Upload();
    }
    void SetupCullingContextParams()
    {
        HLSL::CullingContext cullingContextParams;

        cullingContextParams.cameraIndex = 0;
        cullingContextParams.lightsIndex = 0;
        cullingContextParams.culledInstanceIndex = cullingContext.instancesInView.GetResource().uav.offset;
        cullingContextParams.culledMeshletsIndex = cullingContext.meshletsInView.GetResource().uav.offset;
        cullingContextParams.instancesCounterIndex = cullingContext.instancesCounter.GetResource().uav.offset;
        cullingContextParams.meshletsCounterIndex = cullingContext.meshletsCounter.GetResource().uav.offset;

        cullingContext.cullingContext->Clear();
        cullingContext.cullingContext->Add(cullingContextParams);
        cullingContext.cullingContext->Upload();
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

    // debug only ?
    String name;

    virtual void On(View* _view, ID3D12CommandQueue* queue, String _name)
    {
        ZoneScoped;
        view = _view;
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
        PIXBeginEvent(commandBuffer->cmd, PIX_COLOR_INDEX((BYTE)name.c_str()), name.c_str());
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
        if (commandBuffer->open)
            IOs::Log("{} OPEN !!", name.c_str());
        ID3D12CommandQueue* commandQueue = GPU::instance->graphicQueue;
        commandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&commandBuffer->cmd);
        commandQueue->Signal(commandBuffer->passEnd.fence, ++commandBuffer->passEnd.fenceValue);
    }

    void SetupView(View* view, Resource* RT, uint RTCount, bool clearRT, Resource* depth, bool clearDepth)
    {

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

        // USE : commandBuffer->cmd->BeginRenderPass(); ?
        D3D12_CPU_DESCRIPTOR_HANDLE RTs[8] = {};
        for (uint i = 0; i < RTCount; i++)
        {
            RTs[i] = RT[i].rtv.handle;
        }
        commandBuffer->cmd->OMSetRenderTargets(RTCount, RTs, false, &depth->dsv.handle);

        float4 clearColor(0.4f, 0.1f, 0.2f, 0.0f);
        commandBuffer->cmd->ClearRenderTargetView(GPU::instance->backBuffer.Get().rtv.handle, clearColor.f32, 1, &rect);

        float clearDepthValue(1.0f);
        UINT8 clearStencilValue(0);
        commandBuffer->cmd->ClearDepthStencilView(depth->dsv.handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clearDepthValue, clearStencilValue, 1, &rect);
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

class HZB : public Pass
{
    ViewResource depth;
    ViewResource depthDownSample;
    FfxSpdContextDescription m_InitializationParameters = { 0 };
    FfxSpdContext            m_Context;
public:
    virtual void On(View* view, ID3D12CommandQueue* queue, String _name) override
    {
        Pass::On(view, queue, _name);
        ZoneScoped;

        depth.Register("Depth", view);
        depthDownSample.Register("DepthDownSample", view);
        depthDownSample.Get().CreateTexture(view->resolution, DXGI_FORMAT_R32_FLOAT, true, "DepthDownSample");

        // create backend interface (DX12)
        size_t scratchBufferSize = ffxGetScratchMemorySizeDX12(1);
        void* scratchBuffer = malloc(scratchBufferSize);
        memset(scratchBuffer, 0, scratchBufferSize);
        ffxGetInterfaceDX12(&m_InitializationParameters.backendInterface, ffxGetDeviceDX12(GPU::instance->device), scratchBuffer, scratchBufferSize, 1);

        // Setup all the parameters for this SPD run
        m_InitializationParameters.downsampleFilter = FFX_SPD_DOWNSAMPLE_FILTER_MAX;
        m_InitializationParameters.flags = 0;   // Reset
        m_InitializationParameters.flags |= FFX_SPD_SAMPLER_LOAD;
        m_InitializationParameters.flags |= FFX_SPD_WAVE_INTEROP_LDS;
        m_InitializationParameters.flags |= FFX_SPD_MATH_PACKED;
        ffxSpdContextCreate(&m_Context, &m_InitializationParameters);

    }
    virtual void Off() override
    {
        Pass::Off();
        ZoneScoped;
        ffxSpdContextDestroy(&m_Context);
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

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

        FfxErrorCode errorCode = ffxSpdContextDispatch(&m_Context, &dispatchParameters);
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
    virtual void On(View* view, ID3D12CommandQueue* queue, String _name) override
    {
        Pass::On(view, queue, _name);
        ZoneScoped;
        depth.Register("Depth", view);
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

        Shader& reset = *AssetLibrary::instance->Get<Shader>(cullingResetShader.Get().id, true);
        commandBuffer->SetCompute(reset);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, view->viewWorld->commonResourcesIndices.GetGPUVirtualAddress(0));
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, view->cullingContext.cullingContext->GetGPUVirtualAddress(0));
        commandBuffer->cmd->Dispatch(1, 1, 1);

        Shader& cullingInstances = *AssetLibrary::instance->Get<Shader>(cullingInstancesShader.Get().id, true);
        commandBuffer->SetCompute(cullingInstances);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, view->viewWorld->commonResourcesIndices.GetGPUVirtualAddress(0));
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, view->cullingContext.cullingContext->GetGPUVirtualAddress(0));
        commandBuffer->cmd->Dispatch(cullingInstances.DispatchX(instances.Size()), 1, 1);

        view->cullingContext.instancesInView.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        view->cullingContext.instancesCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        Shader& cullingMeshlets = *AssetLibrary::instance->Get<Shader>(cullingMeshletsShader.Get().id, true);
        commandBuffer->SetCompute(cullingMeshlets);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, view->viewWorld->commonResourcesIndices.GetGPUVirtualAddress(0));
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, view->cullingContext.cullingContext->GetGPUVirtualAddress(0));
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
    void On(View* view, ID3D12CommandQueue* queue, String _name) override
    {
        Pass::On(view, queue, _name);
        ZoneScoped;
        depth.Register("Depth", view);
        depth.Get().CreateDepthTarget(view->resolution, "Depth");
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
public:
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    // indirect draw calls from cullingResult
    void Render(View* view) override
    {
        ZoneScoped;
        Open();
        Close();
    }
};

class Lighting : public Pass
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

class Forward : public Pass
{
    ViewResource depth;
    Components::Handle<Components::Shader> meshShader;
public:
    void On(View* view, ID3D12CommandQueue* queue, String _name) override
    {
        Pass::On(view, queue, _name);
        ZoneScoped;
        depth.Register("Depth", view);
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

        GPU::instance->backBuffer->Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        SetupView(view, &GPU::instance->backBuffer.Get(), 1, true, &depth.Get(), true);

        Shader& shader = *AssetLibrary::instance->Get<Shader>(meshShader.Get().id, true);
        commandBuffer->SetGraphic(shader);

        commandBuffer->cmd->SetGraphicsRootConstantBufferView(0, view->viewWorld->commonResourcesIndices.GetGPUVirtualAddress());
        commandBuffer->cmd->SetGraphicsRootConstantBufferView(1, view->cullingContext.cullingContext->GetGPUVirtualAddress());

        uint maxDraw = view->cullingContext.meshletsInView.Size();
        commandBuffer->cmd->ExecuteIndirect(shader.commandSignature, maxDraw, view->cullingContext.meshletsInView.GetResourcePtr(), 0, view->cullingContext.meshletsCounter.GetResourcePtr(), 0);

        Close();
    }
};

class PostProcess : public Pass
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

class Present : public Pass
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


#define SUBTASKVIEWPASS(pass) tf::Task pass##Task = subflow.emplace([this](){this->pass.Render(this);}).name(#pass)
#define SUBTASKPASS(pass) tf::Task pass##Task = subflow.emplace([this](){this->pass.Render(nullptr);}).name(#pass)
#define SUBTASKRENDERERWORLD(pass) tf::Task pass = subflow.emplace([this, world](){this->pass(world);}).name(#pass)
#define SUBTASKRENDERER(pass) tf::Task pass = subflow.emplace([this](){this->pass();}).name(#pass)

// a view per type of render ? one for main view, one for cubemap, one for minimap ?
// main view render should always be the last one to render ?
class MainView : public View
{
public:
    HZB hzb;
    Skinning skinning;
    Particles particles;
    Spawning spawning;
    Culling culling;
    ZPrepass zPrepass;
    GBuffers gBuffers;
    Lighting lighting;
    Forward forward;
    PostProcess postProcess;
    Present present;

    Resource depthBuffer;

    void On(IOs::WindowInformation& window) override
    {
        View::On(window);

        resolution = window.windowResolution;

        depthBuffer.CreateDepthTarget(resolution, "Depth");

        hzb.On(this, GPU::instance->graphicQueue, "hzb");
        skinning.On(this, GPU::instance->graphicQueue, "skinning");
        particles.On(this, GPU::instance->graphicQueue, "particles");
        spawning.On(this, GPU::instance->graphicQueue, "spawning");
        culling.On(this, GPU::instance->graphicQueue, "culling");
        zPrepass.On(this, GPU::instance->graphicQueue, "zPrepass");
        gBuffers.On(this, GPU::instance->graphicQueue, "gBuffers");
        lighting.On(this, GPU::instance->graphicQueue, "lighting");
        forward.On(this, GPU::instance->graphicQueue, "forward");
        postProcess.On(this, GPU::instance->graphicQueue, "postProcess");
        present.On(this, GPU::instance->graphicQueue, "present");

        //AssetLibrary::instance->LoadMandatory();
    }

    void Off() override
    {
        hzb.Off();
        skinning.Off();
        particles.Off();
        spawning.Off();
        culling.Off();
        zPrepass.Off();
        gBuffers.Off();
        lighting.Off();
        forward.Off();
        postProcess.Off();
        present.Off();

        depthBuffer.Release();

        View::Off();
    }

    tf::Task Schedule(World& world, tf::Subflow& subflow) override
    {
        ZoneScoped;

        tf::Task updateInstances = UpdateInstances(world, subflow);
        tf::Task updateMaterials = UpdateMaterials(world, subflow);
        tf::Task updateLights = UpdateLights(world, subflow);
        tf::Task updateCameras = UpdateCameras(world, subflow);

        tf::Task uploadAndSetup = UploadAndSetup(world, subflow);

        SUBTASKVIEWPASS(hzb);
        SUBTASKVIEWPASS(skinning);
        SUBTASKVIEWPASS(particles);
        SUBTASKVIEWPASS(spawning);
        SUBTASKVIEWPASS(culling);
        SUBTASKVIEWPASS(zPrepass);
        SUBTASKVIEWPASS(gBuffers);
        SUBTASKVIEWPASS(lighting);
        SUBTASKVIEWPASS(forward);
        SUBTASKVIEWPASS(postProcess);
        SUBTASKVIEWPASS(present);

        updateInstances.precede(updateMaterials, uploadAndSetup);
        uploadAndSetup.precede(skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);

        presentTask.succeed(updateInstances, updateMaterials, uploadAndSetup);
        presentTask.succeed(hzbTask, skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);

        return presentTask;
    }

    void Execute() override
    {
        ZoneScoped;
        hzb.Execute();
        skinning.Execute();
        particles.Execute();
        spawning.Execute();
        culling.Execute();
        zPrepass.Execute();
        gBuffers.Execute();
        lighting.Execute();
        forward.Execute();
        postProcess.Execute();
        present.Execute();

    }

    tf::Task UpdateInstances(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        viewWorld->instances.Clear();
        viewWorld->meshletsCount = 0;


#define UpdateInstancesStepSize 128
        ViewWorld& frameWorld = viewWorld.Get();
        
        uint instanceQueryIndex = world.Query(Components::Instance::mask | Components::WorldMatrix::mask, 0);
        uint entityCount = (uint)world.frameQueries[instanceQueryIndex].size();
        frameWorld.instances.Reserve(entityCount);

        uint materialsCount = world.CountQuery(Components::Material::mask, 0);
        frameWorld.materials.Reserve(materialsCount);
        
        tf::Task task = subflow.for_each_index(0, entityCount, UpdateInstancesStepSize,
            [&world, &frameWorld, instanceQueryIndex](int i)
            {
                ZoneScopedN("UpdateInstance");

                std::array<HLSL::Instance, UpdateInstancesStepSize> localInstances;
                uint localMeshletCount = 0;
                uint instanceCount = 0;
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
                    bool materialAdded = frameWorld.materials.Add(World::Entity(instanceCmp.material.index), materialIndex);
                    if (!frameWorld.materials.GetLoaded(materialIndex))
                        continue;

                    // everything should be loaded to be able to draw the instance
                    float4x4 worldMatrix = ComputeWorldMatrix(ent);

                    HLSL::Instance& instance = localInstances[instanceCount];
                    instance.meshIndex = meshIndex;
                    instance.materialIndex = materialIndex;
                    instance.worldMatrix = worldMatrix;

                    // if in range (depending on distance and BC size)
                        // Add to TLAS
                    // count instances with shader

                    instanceCount++;

                    Mesh* mesh = AssetLibrary::instance->Get<Mesh>(meshCmp.id);
                    localMeshletCount += mesh->meshletCount;
                }

                frameWorld.instances.AddRange(localInstances.data(), instanceCount);
                frameWorld.meshletsCount += localMeshletCount;
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
                        Material& material = frameWorld.materials.GetData(materialIndex);

                        material.shaderIndex = AssetLibrary::instance->GetIndex(materialCmp.shader.Get().id);
                        for (uint paramIndex = 0; paramIndex < Components::Material::maxParameters; paramIndex++)
                        {
                            // memcpy ? it is even just a cashline 
                            material.parameters[paramIndex] = materialCmp.prameters[paramIndex];
                        }
                        bool materialReady = true;
                        for (uint texIndex = 0; texIndex < Components::Material::maxTextures; texIndex++)
                        {
                            if (materialCmp.textures[texIndex].index != entityInvalid)
                            {
                                Components::Texture& textureCmp = materialCmp.textures[texIndex].Get();
                                Resource* texture = AssetLibrary::instance->Get<Resource>(textureCmp.id);
                                if (!texture)
                                    materialReady = false;
                                else
                                    material.texturesSRV[texIndex] = texture->srv;
                            }
                        }

                        frameWorld.materials.SetLoaded(materialIndex, materialReady);
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

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, &frameWorld, queryIndex](int i)
            {
                ZoneScopedN("UpdateLights");

            }
        );
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

                HLSL::Camera hlslcamPrevious = {};
                if (this->viewWorld->cameras.Size() > 0)
                {
                    hlslcamPrevious = this->viewWorld->cameras[0];
                }

                this->viewWorld->cameras.Clear();
                for (uint i = 0; i < entityCount; i++)
                {
                    auto& cam = queryResult[i].Get<Components::Camera>();
                    auto& trans = queryResult[i].Get<Components::WorldMatrix>();
                    float4x4 proj = MatrixPerspectiveFovLH(cam.fovY * (3.14f / 180.0f), float(this->resolution.x) / float(this->resolution.y), cam.nearClip, cam.farClip);
                    float4x4 viewProj = mul(inverse(trans.matrix), proj);

                    float4 planes[6];
                    float3 worldCorners[8];
                    float sizeCulling;

                    // compute planes
                    float4x4 mat = mul(inverse(proj), trans.matrix);

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
                        tmp = float4(mul(cube[i], mat).vec);
                        worldCorners[i] = float3((tmp / tmp.w).vec);
                    }

                    //4. generate and store the 6 planes that make up the frustum
                    planes[0] = PlaneFromPoints(worldCorners[0], worldCorners[1], worldCorners[2]); // Near
                    planes[2] = PlaneFromPoints(worldCorners[2], worldCorners[6], worldCorners[4]); // Left
                    planes[3] = PlaneFromPoints(worldCorners[7], worldCorners[3], worldCorners[5]); // Right
                    planes[5] = PlaneFromPoints(worldCorners[1], worldCorners[0], worldCorners[4]); // Bottom
                    planes[1] = PlaneFromPoints(worldCorners[6], worldCorners[7], worldCorners[5]); // Far
                    planes[4] = PlaneFromPoints(worldCorners[2], worldCorners[3], worldCorners[6]); // Top

                    HLSL::Camera hlslcam;

                    hlslcam.viewProj = viewProj;
                    hlslcam.planes[0] = planes[0];
                    hlslcam.planes[1] = planes[1];
                    hlslcam.planes[2] = planes[2];
                    hlslcam.planes[3] = planes[3];
                    hlslcam.planes[4] = planes[4];
                    hlslcam.planes[5] = planes[5];

                    if (options.stopFrustumUpdate)
                    {
                        hlslcam.planes[0] = hlslcamPrevious.planes[0];
                        hlslcam.planes[1] = hlslcamPrevious.planes[1];
                        hlslcam.planes[2] = hlslcamPrevious.planes[2];
                        hlslcam.planes[3] = hlslcamPrevious.planes[3];
                        hlslcam.planes[4] = hlslcamPrevious.planes[4];
                        hlslcam.planes[5] = hlslcamPrevious.planes[5];
                    }

                    this->viewWorld->cameras.Add(hlslcam);
                }

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
                this->viewWorld->instances.Upload();
                this->cullingContext.instancesInView.Resize(this->viewWorld->instances.Size());
                this->cullingContext.meshletsInView.Resize(this->viewWorld->meshletsCount);
                SetupViewParams();
                SetupCullingContextParams();
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

        editor.On(this, GPU::instance->graphicQueue, "editor");
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

class Renderer
{
public:
    static Renderer* instance;
    MeshStorage meshStorage;
    MainView mainView;
    EditorView editorView;

    void On(IOs::WindowInformation& window)
    {
        instance = this;
        meshStorage.On();
        mainView.On(window);
        editorView.On(window);
    }
    
    void Off()
    {
        editorView.Off();
        mainView.Off();
        meshStorage.Off();
        instance = nullptr;
    }

    void Schedule(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        Profiler::instance->instancesCount = mainView.viewWorld->instances.Size();
        Profiler::instance->meshletsCount = mainView.viewWorld->meshletsCount;
        Profiler::instance->verticesCount = 0;

        auto mainViewEndTask = mainView.Schedule(world, subflow);
        auto editorViewEndTask = editorView.Schedule(world, subflow);

        SUBTASKRENDERER(ExecuteFrame);
        SUBTASKRENDERER(WaitFrame);
        SUBTASKRENDERER(PresentFrame);

        ExecuteFrame.succeed(mainViewEndTask, editorViewEndTask);
        ExecuteFrame.precede(WaitFrame);
        WaitFrame.precede(PresentFrame);
    }

    void ExecuteFrame()
    {
        ZoneScoped;
        HRESULT hr;

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
        Fence& previousFrame = editorView.editor.commandBuffer.Get(GPU::instance->frameIndex ? 0 : 1).passEnd;
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

        Resource::CleanUploadResources();
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
