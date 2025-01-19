#pragma once

#include <format>
#include <algorithm>
#include <vector>
#include <map>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxgi1_4.h>
#include "D3d12sdklayers.h"
#include "../../Third/D3D12MemoryAllocator-master/include/D3D12MemAlloc.h"

extern "C" { _declspec(dllexport) extern const UINT D3D12SDKVersion = 4; }
extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

// current version of pix and thread is not good ! do define No_Thread for pix to work
#define USE_PIX
#ifdef USE_PIX
#include "pix3.h"
#endif

#include "Shaders/structs.hlsl"

#define FRAMEBUFFERING 2

struct SRV
{
    uint offset;
};

struct UAV
{
    uint offset;
};

struct RTV
{
    uint offset;
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    ID3D12Resource* raw;
};

struct DSV
{
    uint offset;
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
};

template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubObjectType, typename T>
struct alignas(void*) StreamSubObject
{
    StreamSubObject() = default;

    StreamSubObject(const T& rhs)
        : Type(SubObjectType), InnerObject(rhs)
    {
    }
    operator T& () { return InnerObject; }

private:
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = SubObjectType;
    T InnerObject{};
};

struct PipelineStateStream
{
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> VS;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> PS;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, D3D12_SHADER_BYTECODE> CS;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE> AS;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> MS;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> RTFormats;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> DSVFormat;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1> DepthStencil;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> Rasterizer;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> Blend;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> PrimitiveTopology;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> InputLayout;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> pRootSignature;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> SampleMask;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> SampleDesc;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE> StripCutValue;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC> StreamOutput;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS> Flags;
    StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, UINT> NodeMask;

    PipelineStateStream()
    {
        PrimitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        D3D12_RASTERIZER_DESC& raster = Rasterizer;
        raster.FillMode = D3D12_FILL_MODE_SOLID;
        raster.CullMode = D3D12_CULL_MODE_NONE;
        raster.FrontCounterClockwise = TRUE;
        raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        raster.DepthClipEnable = FALSE;
        raster.MultisampleEnable = FALSE;
        raster.AntialiasedLineEnable = FALSE;
        raster.ForcedSampleCount = 0;

        D3D12_RT_FORMAT_ARRAY& rts = RTFormats;
        rts.NumRenderTargets = 1;
        rts.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

        DXGI_SAMPLE_DESC& samples = SampleDesc;
        samples.Count = 1;
        samples.Quality = 0;

        DXGI_FORMAT& dsv = DSVFormat;
        dsv = DXGI_FORMAT_D32_FLOAT;

        D3D12_DEPTH_STENCIL_DESC1& ds = DepthStencil;
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        ds.StencilEnable = FALSE;
        ds.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        ds.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        ds.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        ds.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        ds.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        ds.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_NEVER;
        ds.BackFace = ds.FrontFace;

        D3D12_BLEND_DESC& blend = Blend;
        blend.AlphaToCoverageEnable = FALSE;
        blend.IndependentBlendEnable = FALSE;
        blend.RenderTarget[0].BlendEnable = FALSE;
        blend.RenderTarget[0].LogicOpEnable = FALSE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_COLOR;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    }
};

struct Shader
{
    ID3D12CommandSignature* commandSignature;
    ID3D12RootSignature* rootSignature;
    ID3D12PipelineState* pso;

    std::map<String, __time64_t> creationTime;
    bool NeedReload()
    {
        if (editorState.shaderReload)
        {
            for (auto& item : creationTime)
            {
                auto file = item.first;
                struct stat result;
                if (stat(file.c_str(), &result) == 0)
                {
                    auto mod_time = result.st_mtime;
                    if (mod_time != item.second)
                        return true;
                }
            }
        }
        return false;
    }
};

struct Fence
{
    ID3D12Fence* fence = NULL;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent;
};

struct CommandBuffer
{
    // everything is a graphic command, why no compute list ?
    ID3D12GraphicsCommandList7* cmd = NULL;
    ID3D12CommandAllocator* cmdAlloc = NULL;
    ID3D12CommandQueue* queue = NULL;
    Fence passEnd;
    Fence waitFor;
    uint profileIdx;
    bool open;

    void Set(Shader& shader);
    void On(bool asyncCompute, String name);
    void Off();
};

class Resource
{
public:
    D3D12MA::Allocation* allocation{};
    uint stride{};
    SRV srv{ UINT32_MAX };
    UAV uav{ UINT32_MAX };
    RTV rtv{ UINT32_MAX, 0, 0 };
    DSV dsv{ UINT32_MAX, 0 };

    void CreateTexture(uint2 resolution, DXGI_FORMAT format, String name = "Texture");
    void CreateRenderTarget(uint2 resolution, DXGI_FORMAT format, String name = "Texture");
    void CreateDepthTarget(uint2 resolution, String name = "Texture");
    void CreateBuffer(uint size, uint stride, bool upload = false, String name = "Buffer");
    template <typename T>
    void CreateBuffer(uint count, String name = "Buffer");
    template <typename T>
    void CreateUploadBuffer(uint count, String name = "Buffer");
    void CreateReadBackBuffer(uint size, String name = "ReadBackBuffer");
    void BackBuffer(ID3D12Resource* backBuffer);
    void Release();
    ID3D12Resource* GetResource();
    uint BufferSize();
    void Upload(void* data, uint dataSize, CommandBuffer& cb, uint offset = 0);
    void Transition(CommandBuffer& cb, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);
    void Transition(CommandBuffer& cb, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter, ID3D12Resource* resource);
    static void CleanUploadResources(bool everything = false);

    static std::mutex lock;
    static std::vector<std::tuple<uint, D3D12MA::Allocation*>> uploadResources;
    static std::vector<D3D12MA::Allocation*> allResources;
    static std::vector<String> allResourcesNames;
};

struct DescriptorHeap
{
    static const uint maxDescriptorCount = 65535;
    ID3D12DescriptorHeap* globalDescriptorHeap;
    Slots globalDescriptorHeapSlots;
    int descriptorIncrementSize;
    ID3D12DescriptorHeap* rtvDescriptorHeap;
    Slots rtvDescriptorHeapSlots;
    int rtvdescriptorIncrementSize;
    ID3D12DescriptorHeap* dsvDescriptorHeap;
    Slots dsvDescriptorHeapSlots;
    int dsvdescriptorIncrementSize;

    void On(ID3D12Device10* device)
    {
        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            heapDesc.NumDescriptors = maxDescriptorCount;

            auto hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&globalDescriptorHeap));
            if (FAILED(hr))
            {
                //GPU::PrintDeviceRemovedReason(hr);
            }
            globalDescriptorHeap->SetName(L"Global Desc Heap");

            globalDescriptorHeapSlots.On(maxDescriptorCount);

            descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            heapDesc.NumDescriptors = maxDescriptorCount;

            auto hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvDescriptorHeap));
            if (FAILED(hr))
            {
                //GPU::PrintDeviceRemovedReason(hr);
            }
            rtvDescriptorHeap->SetName(L"RTV Desc Heap");

            rtvDescriptorHeapSlots.On(maxDescriptorCount);

            rtvdescriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }

        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            heapDesc.NumDescriptors = maxDescriptorCount;

            auto hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvDescriptorHeap));
            if (FAILED(hr))
            {
                //GPU::PrintDeviceRemovedReason(hr);
            }
            dsvDescriptorHeap->SetName(L"DSV Desc Heap");

            dsvDescriptorHeapSlots.On(maxDescriptorCount);

            dsvdescriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        }
    }
    void Off()
    {
        globalDescriptorHeap->Release();
        rtvDescriptorHeap->Release();
        dsvDescriptorHeap->Release();
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetSlot(uint* offset, Slots* slots, ID3D12DescriptorHeap* heap, int descriptorIncrementSize)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        if (*offset != UINT32_MAX)
        {
            slots->Release(*offset);
        }

        *offset = (uint)slots->Get();
        handle.ptr = static_cast<SIZE_T>(heap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(*offset) * UINT64(descriptorIncrementSize));

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetGlobalSlot(SRV& srv)
    {
        return GetSlot(&srv.offset, &globalDescriptorHeapSlots, globalDescriptorHeap, descriptorIncrementSize);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetGlobalSlot(UAV& uav)
    {
        return GetSlot(&uav.offset, &globalDescriptorHeapSlots, globalDescriptorHeap, descriptorIncrementSize);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTVSlot(RTV& rtv)
    {
        return GetSlot(&rtv.offset, &rtvDescriptorHeapSlots, rtvDescriptorHeap, rtvdescriptorIncrementSize);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVSlot(DSV& dsv)
    {
        return GetSlot(&dsv.offset, &dsvDescriptorHeapSlots, dsvDescriptorHeap, dsvdescriptorIncrementSize);
    }
};

// c�est crade, c�est parceque GPU est pas encore definit et j�ai pas acces a GPU::frameIndex;
uint g_frameIndex;
template<class T>
class PerFrame
{
    T data[FRAMEBUFFERING];
public:
    T& Get(uint frameIndex)
    {
        return data[frameIndex];
    }
    T& Get()
    {
        return Get(g_frameIndex);
    }
    T* operator -> ()
    {
        return &Get();
    }
};

class GPU
{
public:
    static GPU* instance;
    IDXGIFactory4* dxgiFactory{};
    IDXGIAdapter3* adapter{};
    ID3D12Device10* device{};

    struct Features
    {
        bool vSync : 1;
        bool fullscreen : 1;
        bool meshShader : 1;
        bool raytracing : 1;
    };
    Features features{};

    ID3D12CommandQueue* graphicQueue{};
    ID3D12CommandQueue* computeQueue{};

    IDXGISwapChain3* swapChain{};

    PerFrame<Resource> backBuffer;

    D3D12MA::Allocator* allocator{};

    DescriptorHeap descriptorHeap;

    uint frameIndex{};
    uint frameNumber{};


    void On(IOs::WindowInformation* window)
    {
        ZoneScoped;
        instance = this;
        features.vSync = window->usevSync;
        features.fullscreen = window->fullScreen;
        CreateDevice(window);
        descriptorHeap.On(device);
        CreateSwapChain(window);
        CreateMemoryAllocator();

    }

    void Off()
    {
        ZoneScoped;
        descriptorHeap.Off();
        graphicQueue->Release();
        computeQueue->Release();

        Resource::CleanUploadResources(true);
        for (uint i = 0; i < Resource::allResourcesNames.size(); i++)
        {
            IOs::Log("{}", Resource::allResourcesNames[i].c_str());
        }

        allocator->Release();
        //swapChain->Release();
        device->Release();
        adapter->Release();
        dxgiFactory->Release();
    }

    void CreateDevice(IOs::WindowInformation* window)
    {
        ZoneScoped;
        HRESULT hr;

        uint debugFlags = 0;
#if defined(_DEBUG)
        ID3D12Debug* debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            debugController->Release();
        }
        debugFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
        CreateDXGIFactory2(debugFlags, IID_PPV_ARGS(&dxgiFactory));

        int adapterIndex = 0;
        bool adapterFound = false;
        DXGI_ADAPTER_DESC2 desc;
        while (dxgiFactory->EnumAdapters1(adapterIndex, (IDXGIAdapter1**)&adapter) != DXGI_ERROR_NOT_FOUND)
        {
            adapterIndex++;
            adapter->GetDesc2(&desc);

            if (String(WCharToString(desc.Description)).find("NVIDIA") == -1) continue;
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, _uuidof(ID3D12Device14), nullptr);
            if (SUCCEEDED(hr))
            {
                adapterFound = true;
                break;
            }
        }
        if (!adapterFound)
        {
            return;
        }
        hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            return;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS7 featureData = {};
        device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &featureData, sizeof(featureData));
        features.meshShader = (featureData.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);


        D3D12_COMMAND_QUEUE_DESC commandQueueDesc;
        commandQueueDesc = {};
        commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&graphicQueue));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            return;
        }
        commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_COMPUTE;
        hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&computeQueue));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            return;
        }
    }

    void CreateSwapChain(IOs::WindowInformation* window)
    {
        ZoneScoped;
        HRESULT hr;
        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = FRAMEBUFFERING;
        swapChainDesc.Width = (int)window->windowResolution.x;
        swapChainDesc.Height = (int)window->windowResolution.y;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainDescFullscreen = {};
        swapChainDescFullscreen.RefreshRate = { 0, 0 }; // forcing 1, 60 screw the thing ups and we end up at 30hz https://www.gamedev.net/forums/topic/687484-correct-way-of-creating-the-swapchain-fullscreenwindowedvsync-about-refresh-rate/5337872/
        swapChainDescFullscreen.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        swapChainDescFullscreen.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        swapChainDescFullscreen.Windowed = !features.fullscreen;

        IDXGISwapChain1* tempSwapChain;

        hr = dxgiFactory->CreateSwapChainForHwnd(
            graphicQueue, // the queue will be flushed once the swap chain is created
            window->windowHandle,
            &swapChainDesc, // give it the swap chain description we created above
            features.fullscreen ? &swapChainDescFullscreen : nullptr,
            nullptr,
            &tempSwapChain // store the created swap chain in a temp IDXGISwapChain interface
        );
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            return;
        }

        swapChain = static_cast<IDXGISwapChain3*>(tempSwapChain);

        //dxgiFactory->MakeWindowAssociation(window->windowHandle, DXGI_MWA_NO_ALT_ENTER);

        for (uint i = 0; i < swapChainDesc.BufferCount; i++)
        {
            ID3D12Resource2* res;
            HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&res));
            backBuffer.Get(i).BackBuffer(res);
        }
    }

    void CreateMemoryAllocator()
    {
        ZoneScoped;
        HRESULT hr;
        D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
        allocatorDesc.pDevice = device;
        allocatorDesc.pAdapter = adapter;
        allocatorDesc.Flags = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED | D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED; // These flags are optional but recommended.

        hr = D3D12MA::CreateAllocator(&allocatorDesc, &allocator);
    }

    void Resize(IOs::WindowInformation* window)
    {
        ZoneScoped;
        if (swapChain)
        {
            DXGI_SWAP_CHAIN_DESC1 desc;
            swapChain->GetDesc1(&desc);

            for (uint i = 0; i < desc.BufferCount; i++)
            {
                ID3D12Resource2* res;
                HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&res));
                res->Release();
            }

            swapChain->ResizeBuffers(desc.BufferCount, desc.Width, desc.Height, desc.Format, desc.Flags);

            for (uint i = 0; i < desc.BufferCount; i++)
            {
                ID3D12Resource2* res;
                HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&res));
                backBuffer.Get(i).BackBuffer(res);
            }
        }
    }

    void FrameStart()
    {
        frameNumber++;
        frameIndex = swapChain->GetCurrentBackBufferIndex();
        g_frameIndex = frameIndex;
    }

    static __declspec(noinline) void PrintDeviceRemovedReason(HRESULT hr)
    {
        HRESULT reason = instance->device->GetDeviceRemovedReason();
        IOs::Log("Device removed! DXGI_ERROR code: 0x{} | 0x{}", (int)reason, (int)hr);
    }
};
GPU* GPU::instance;

// definitions of Resource
void Resource::CreateTexture(uint2 resolution, DXGI_FORMAT format, String name)
{
    //ZoneScoped;
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = resolution.x;
    resourceDesc.Height = resolution.y;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        NULL,
        &allocation,
        IID_NULL, NULL);

    std::wstring wname = name.ToWString();
    allocation->SetName(wname.c_str());
    allocation->GetResource()->SetName(wname.c_str());
    lock.lock();
    allResources.push_back(allocation);
    allResourcesNames.push_back(name);
    lock.unlock();

    {
        auto desc = GetResource()->GetDesc();
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        srv.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = desc.Format;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MipLevels = desc.MipLevels;
        GPU::instance->device->CreateShaderResourceView(GetResource(), &SRVDesc, handle);
    }

    {
        auto desc = GetResource()->GetDesc();
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        uav.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(uav.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        UAVDesc.Format = desc.Format;
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        UAVDesc.Texture2D.MipSlice = 0;
        GPU::instance->device->CreateUnorderedAccessView(GetResource(), nullptr, &UAVDesc, handle);
    }
}

void Resource::CreateRenderTarget(uint2 resolution, DXGI_FORMAT format, String name)
{
    //ZoneScoped;
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = resolution.x;
    resourceDesc.Height = resolution.y;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        NULL,
        &allocation,
        IID_NULL, NULL);

    std::wstring wname = name.ToWString();
    allocation->SetName(wname.c_str());
    allocation->GetResource()->SetName(wname.c_str());
    lock.lock();
    allResources.push_back(allocation);
    allResourcesNames.push_back(name);
    lock.unlock();

    {
        auto desc = GetResource()->GetDesc();
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        srv.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = desc.Format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT : desc.Format;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MipLevels = desc.MipLevels;
        GPU::instance->device->CreateShaderResourceView(GetResource(), &SRVDesc, handle);
    }

    /*
    {
        auto desc = GetResource()->GetDesc();
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        uav.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(uav.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        UAVDesc.Format = desc.Format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT : desc.Format;
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        UAVDesc.Texture2D.MipSlice = 0;
        GPU::instance->device->CreateUnorderedAccessView(GetResource(), nullptr, &UAVDesc, handle);
    }
    */

    {
        rtv.handle = GPU::instance->descriptorHeap.GetRTVSlot(rtv);

        auto desc = GetResource()->GetDesc();
        D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
        RTVDesc.Format = desc.Format;
        RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        GPU::instance->device->CreateRenderTargetView(GetResource(), &RTVDesc, rtv.handle);
    }
}

void Resource::CreateDepthTarget(uint2 resolution, String name)
{
    //ZoneScoped;
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = resolution.x;
    resourceDesc.Height = resolution.y;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clear;
    clear.DepthStencil = { 1,0 };
    clear.Format = resourceDesc.Format;

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        &allocation,
        IID_NULL, NULL);

    std::wstring wname = name.ToWString();
    allocation->SetName(wname.c_str());
    allocation->GetResource()->SetName(wname.c_str());
    lock.lock();
    allResources.push_back(allocation);
    allResourcesNames.push_back(name);
    lock.unlock();

    {
        auto desc = GetResource()->GetDesc();
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        srv.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MipLevels = desc.MipLevels;
        GPU::instance->device->CreateShaderResourceView(GetResource(), &SRVDesc, handle);
    }

    /*
    {
        auto desc = GetResource()->GetDesc();
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        uav.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(uav.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        UAVDesc.Format = DXGI_FORMAT_R32_FLOAT;
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        UAVDesc.Texture2D.MipSlice = 0;
        GPU::instance->device->CreateUnorderedAccessView(GetResource(), nullptr, &UAVDesc, handle);
    }
    */

    {
        dsv.handle = GPU::instance->descriptorHeap.GetDSVSlot(dsv);

        auto desc = GetResource()->GetDesc();
        D3D12_DEPTH_STENCIL_VIEW_DESC DepthDesc = {};
        DepthDesc.Format = desc.Format;
        DepthDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        GPU::instance->device->CreateDepthStencilView(GetResource(), &DepthDesc, dsv.handle);
    }
}

void Resource::CreateBuffer(uint size, uint _stride, bool upload, String name)
{
    //ZoneScoped;

    stride = _stride;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    if (upload)
    {
        allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    }

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        NULL,
        &allocation,
        IID_NULL, NULL);
    if (FAILED(hr))
    {
        IOs::Log("Buffer creation failed {}", hr);
    }

    std::wstring wname = name.ToWString();
    allocation->SetName(wname.c_str());
    allocation->GetResource()->SetName(wname.c_str());
    lock.lock();
    allResources.push_back(allocation);
    allResourcesNames.push_back(name.c_str());
    lock.unlock();

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(srv);

        auto desc = GetResource()->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        if (stride > 1)
        {
            SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            SRVDesc.Buffer = {};
            SRVDesc.Buffer.FirstElement = 0;
            SRVDesc.Buffer.NumElements = (UINT)(desc.Width / stride);
            SRVDesc.Buffer.StructureByteStride = (UINT)stride;
            SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        }
        else
        {
            SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            SRVDesc.Buffer = {};
            SRVDesc.Buffer.FirstElement = 0;
            SRVDesc.Buffer.NumElements = (UINT)(desc.Width / 4);
            SRVDesc.Buffer.StructureByteStride = 0;
            SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        }
        GPU::instance->device->CreateShaderResourceView(GetResource(), &SRVDesc, handle);
    }

    if (!upload)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(uav);

        auto desc = GetResource()->GetDesc();
        D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        if (stride >= 1)
        {
            UAVDesc.Buffer = {};
            UAVDesc.Buffer.FirstElement = 0;
            UAVDesc.Buffer.NumElements = (UINT)(desc.Width / stride);
            UAVDesc.Buffer.StructureByteStride = (UINT)stride;
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        }
        else
        {
            UAVDesc.Buffer = {};
            UAVDesc.Buffer.FirstElement = 0;
            UAVDesc.Buffer.NumElements = (UINT)(desc.Width / 4);
            UAVDesc.Buffer.StructureByteStride = 0;
            UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            UAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        }
        GPU::instance->device->CreateUnorderedAccessView(GetResource(), nullptr, &UAVDesc, handle);
    }
}
template <typename T>
void Resource::CreateBuffer(uint count, String name)
{
    CreateBuffer(sizeof(T) * count, sizeof(T), false, name);
}
template <typename T>
void Resource::CreateUploadBuffer(uint count, String name)
{
    CreateBuffer(sizeof(T) * count, sizeof(T), true, name);
}

void Resource::CreateReadBackBuffer(uint size, String name)
{
    //ZoneScoped;
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_READBACK;

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        NULL,
        &allocation,
        IID_NULL, NULL);

    std::wstring wname = name.ToWString();
    allocation->SetName(wname.c_str());
    lock.lock();
    allResources.push_back(allocation);
    allResourcesNames.push_back(name);
    lock.unlock();

    /*
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(srv);

        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = resourceDesc.Format;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        SRVDesc.Texture2D.MipLevels = resourceDesc.MipLevels;
        GPU::instance->device->CreateShaderResourceView(allocation->GetResource(), &SRVDesc, handle);
    }

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(uav);

        D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        UAVDesc.Format = resourceDesc.Format;
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        UAVDesc.Texture2D.MipSlice = 0;
        GPU::instance->device->CreateUnorderedAccessView(allocation->GetResource(), nullptr, &UAVDesc, handle);
    }
    */
}

void Resource::BackBuffer(ID3D12Resource* backBuffer)
{
    rtv.raw = backBuffer;

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(srv);

        auto desc = GetResource()->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = desc.Format;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MipLevels = desc.MipLevels;
        GPU::instance->device->CreateShaderResourceView(GetResource(), &SRVDesc, handle);
    }

    /*
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(uav);

        auto desc = GetResource()->GetDesc();
        D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        UAVDesc.Format = desc.Format;
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        UAVDesc.Texture2D.MipSlice = 0;
        GPU::instance->device->CreateUnorderedAccessView(GetResource(), nullptr, &UAVDesc, handle);
    }
    */

    {
        rtv.handle = GPU::instance->descriptorHeap.GetRTVSlot(rtv);

        auto desc = GetResource()->GetDesc();
        D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
        RTVDesc.Format = desc.Format;
        RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        GPU::instance->device->CreateRenderTargetView(GetResource(), &RTVDesc, rtv.handle);
    }

}

void Resource::Release()
{
    if (allocation)
    {
        for (int j = (int)allResources.size() - 1; j >= 0; j--)
        {
            if (allResources[j] == allocation)
            {
                allResources.erase(allResources.begin() + j);
                allResourcesNames.erase(allResourcesNames.begin() + j);
            }
        }
        allocation->Release();
    }
    allocation = nullptr;
}

ID3D12Resource* Resource::GetResource()
{
    if (rtv.raw)
        return rtv.raw;
    if(allocation)
        return allocation->GetResource();
    return nullptr;
}

uint Resource::BufferSize()
{
    return GetResource()->GetDesc().Width;
}

void Resource::Upload(void* data, uint dataSize, CommandBuffer& cb, uint offset)
{
    //ZoneScoped;
    if (options.stopBufferUpload)
        return;
    //ZoneScopedN("Resource::Upload");
    D3D12MA::Allocation* uploadAllocation;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = dataSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        NULL,
        &uploadAllocation,
        IID_NULL, NULL);

    uploadAllocation->SetName(L"UploadBuffer");
    lock.lock();
    uploadResources.push_back({ GPU::instance->frameNumber, uploadAllocation });
    allResources.push_back(uploadAllocation);
    allResourcesNames.push_back("UploadBuffer");
    lock.unlock();

    void* buf;
    uploadAllocation->GetResource()->Map(0, nullptr, &buf);
    memcpy(buf, data, dataSize);
    uploadAllocation->GetResource()->Unmap(0, nullptr);

    lock.lock();
    cb.cmd->CopyBufferRegion(allocation->GetResource(), offset, uploadAllocation->GetResource(), 0, dataSize);
    lock.unlock();
}

void Resource::Transition(CommandBuffer& cb, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{
    Transition(cb, stateBefore, stateAfter, GetResource());
}

void Resource::Transition(CommandBuffer& cb, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER trans{ D3D12_RESOURCE_BARRIER_TYPE_TRANSITION , D3D12_RESOURCE_BARRIER_FLAG_NONE , {resource, 0, stateBefore, stateAfter} };
    cb.cmd->ResourceBarrier(1, &trans);
}

void Resource::CleanUploadResources(bool everything)
{
    //ZoneScoped;
    uint frameNumberThreshold = GPU::instance->frameNumber - 2;
    if (everything)
        frameNumberThreshold = UINT_MAX;
    for (int i = (int)uploadResources.size() - 1; i >= 0 ; i--)
    {
        if (std::get<0>(uploadResources[i]) < frameNumberThreshold)
        {
            D3D12MA::Allocation* alloc = std::get<1>(uploadResources[i]);
            for (int j = (int)allResources.size() - 1; j >= 0; j--)
            {
                if (allResources[j] == alloc)
                {
                    allResources.erase(allResources.begin() + j);
                    allResourcesNames.erase(allResourcesNames.begin() + j);
                }
            }
            {
                alloc->Release();
            }
            uploadResources.erase(uploadResources.begin() + i);
        }
    }
}

std::mutex Resource::lock;
std::vector<std::tuple<uint, D3D12MA::Allocation*>> Resource::uploadResources;
std::vector<D3D12MA::Allocation*> Resource::allResources;
std::vector<String> Resource::allResourcesNames;
// end of definitions of Resource

// definitions of CommandBuffer   
void CommandBuffer::Set(Shader& shader)
{
    // SetDescriptorHeaps must be set before SetPipelineState because of dynamic indexing in descriptorHeap
    cmd->SetDescriptorHeaps(1, &GPU::instance->descriptorHeap.globalDescriptorHeap);
    cmd->SetGraphicsRootSignature(shader.rootSignature);
    cmd->SetPipelineState(shader.pso);
    //cmd->SetGraphicsRootDescriptorTable();
}
void CommandBuffer::On(bool asyncCompute, String name)
{
    D3D12_COMMAND_LIST_TYPE type = asyncCompute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue = asyncCompute ? GPU::instance->computeQueue : GPU::instance->graphicQueue;
    HRESULT hr = GPU::instance->device->CreateCommandAllocator(type, IID_PPV_ARGS(&cmdAlloc));
    if (FAILED(hr))
    {
        GPU::PrintDeviceRemovedReason(hr);
    }
    std::wstring wname = name.ToWString();
    hr = cmdAlloc->SetName(wname.c_str());
    if (FAILED(hr))
    {
        GPU::PrintDeviceRemovedReason(hr);
    }
    hr = GPU::instance->device->CreateCommandList(0, type, cmdAlloc, NULL, IID_PPV_ARGS(&cmd));
    if (FAILED(hr))
    {
        GPU::PrintDeviceRemovedReason(hr);
    }
    std::wstring wname2 = name.ToWString();
    hr = cmd->SetName(wname2.c_str());
    if (FAILED(hr))
    {
        GPU::PrintDeviceRemovedReason(hr);
    }
    hr = cmd->Close();
    if (FAILED(hr))
    {
        GPU::PrintDeviceRemovedReason(hr);
    }
    hr = GPU::instance->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&passEnd.fence));
    if (FAILED(hr))
    {
        GPU::PrintDeviceRemovedReason(hr);
    }
    passEnd.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (passEnd.fenceEvent == nullptr)
    {
        GPU::PrintDeviceRemovedReason(hr);
    }
}
void CommandBuffer::Off()
{
    cmd->Release();
    cmdAlloc->Release();
    passEnd.fence->Release();
}
// end of definitions of CommandBuffer

/*
template <class T>
class StructuredBuffer // JUST use Resource !!!!
{
public:
    Resource gpuData;

    uint Size()
    {
        return cpuData.size();
    }
    void Resize(uint size)
    {
        cpuData.resize(size);
    }
    void Reserve(uint size)
    {
        cpuData.reserve(size);
    }
    Resource& GetResource()
    {
        return gpuData;
    }
    ID3D12Resource* GetResourcePtr()
    {
        return gpuData.GetResource();
    }
    void Release()
    {
        gpuData.Release();
    }
};
*/

template <class T>
class StructuredUploadBuffer
{
public:
    std::vector<T> cpuData;
    Resource gpuData;
    std::mutex lock;

    void Clear()
    {
        cpuData.clear();
    }
    uint Add(const T& value)
    {
        cpuData.push_back(value);
        return (uint)cpuData.size();
    }
    uint AddUnique(const T& value)
    {
        uint index = 0;
        for (; index < cpuData.size(); index++)
        {
            if(memcmp(&cpuData[index], &value, sizeof(T)) == 0)
                return index;
        }
        cpuData.push_back(value);
        return index;
    }
    void AddRange(T* data, uint count)
    {
        lock.lock();
        cpuData.reserve(cpuData.size() + count);
        for (uint i = 0; i < count; i++)
        {
            cpuData.push_back(data[i]);
        }
        //std::atomic_thread_fence(std::memory_order_acquire);
        //MemoryBarrier;
        lock.unlock();
    }
    uint Size()
    {
        return (uint)cpuData.size();
    }
    void Resize(uint size)
    {
        cpuData.resize(size);
    }
    void Reserve(uint size)
    {
        cpuData.reserve(size);
    }
    T& operator [] (uint index)
    {
        return cpuData[index];
    }
    Resource& GetResource()
    {
        return gpuData;
    }
    ID3D12Resource* GetResourcePtr()
    {
        return gpuData.GetResource();
    }
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress(uint index)
    {
        if (gpuData.GetResource() == 0)
            return 0;
        return gpuData.GetResource()->GetGPUVirtualAddress() + (index * sizeof(T));
    }
    void Upload()
    {
        ZoneScoped;
        if (options.stopBufferUpload)
            return;
        if (cpuData.size() <= 0)
            return;
        if (gpuData.allocation == nullptr || gpuData.BufferSize() < cpuData.size() * sizeof(T))
        {
            gpuData.Release();
            gpuData.CreateUploadBuffer<T>(max(uint1(cpuData.size()), uint1(1)), typeid(T).name());
        }
        void* buf;
        auto hr = GetResourcePtr()->Map(0, nullptr, &buf);
        if (SUCCEEDED(hr))
        {
            memcpy(buf, cpuData.data(), sizeof(T) * cpuData.size());
            GetResourcePtr()->Unmap(0, nullptr);
        }
    }
    void Release()
    {
        gpuData.Release();
    }
};

class ReadBackBuffer
{
public:
    byte* cpuData;
    Resource gpuData;

    Resource& GetResource()
    {
        return gpuData;
    }
    ID3D12Resource* GetResourcePtr()
    {
        return gpuData.GetResource();
    }
    // resource must be in common state. Always ?
    void ReadBack(Resource& resource, CommandBuffer& cb)
    {
        ZoneScoped;
        if (gpuData.GetResource() == nullptr)
        {
            // can�t use resource.allocation->GetSize().... it give the entire DMA page size ?
            auto descsource = resource.GetResource()->GetDesc();
            gpuData.CreateReadBackBuffer((uint)descsource.Width);
        }
        resource.Transition(cb, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cb.cmd->CopyResource(GetResourcePtr(), resource.GetResource());
        resource.Transition(cb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    }
    template <class T>
    void Get()
    {
        void* buf;
        HRESULT hr = GetResourcePtr()->Map(0, nullptr, &buf);
        memcpy(cpuData, buf, gpuData.allocation->GetSize());
        GetResourcePtr()->Unmap(0, nullptr);
    }

    void Release()
    {
        gpuData.Release();
    }
};

// https://github.com/TheRealMJP/DeferredTexturing/blob/experimental/SampleFramework12/v1.01/Graphics/Profiler.cpp
// use https://github.com/Raikiri/LegitProfiler for display ?
struct Profiler
{
#define QUEUECOUNT 2
    static Profiler* instance;
    struct ProfileData
    {
        LPCSTR name = nullptr;

        bool QueryStarted = false;
        bool QueryFinished = false;
        bool Active = false;

        bool CPUProfile = false;
        INT64 StartTime = 0;
        INT64 EndTime = 0;

        static const UINT64 FilterSize = 64;
        double TimeSamples[FilterSize] = { };
        UINT64 CurrSample = 0;
    };

    UINT64 maxProfiles = 128;
    UINT64 numProfiles = 0;
    ID3D12QueryHeap* queryHeap = NULL;
    Resource buffer;
    ReadBackBuffer readbackBuffer;
    std::vector<ProfileData> profiles;

    void On()
    {
        ZoneScoped;
        D3D12_QUERY_HEAP_DESC queryheapDesc = { };
        queryheapDesc.Count = (UINT)(maxProfiles * QUEUECOUNT);
        queryheapDesc.NodeMask = 0;
        queryheapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        GPU::instance->device->CreateQueryHeap(&queryheapDesc, IID_PPV_ARGS(&queryHeap));

        profiles.resize(maxProfiles);

        /*
        for (int i = 0; i < FRAMEBUFFERING; i++)
        {
            String name = std::format("profilerData_{}", i);
            buffer[i].CreateBuffer<UINT64>((UINT)(maxProfiles * FRAMEBUFFERING * QUEUECOUNT));
            //buffer.Get(i)->gpuData.From(device->memory.GetUAV(device->profiler.Buffer[i].value.stride, device->profiler.Buffer[i].value.capacity, n, D3D12_RESOURCE_STATE_COPY_DEST));
        }
        */
        //readbackBuffer.value.SetCapacity((UINT)(maxProfiles * FRAMEBUFFERING * QUEUECOUNT));
        //readbackBuffer.resource.From(device->memory.GetReadBack(device->profiler.Buffer[0].value.stride, device->profiler.Buffer[0].value.capacity, "profilerDataReadBack"));

        buffer.CreateBuffer<UINT64>((UINT)(maxProfiles * FRAMEBUFFERING * QUEUECOUNT), "Profile");

        instance = this;
    }

    UINT64 StartProfile(CommandBuffer& cb, const LPCSTR name)
    {
        ZoneScoped;
        UINT64 profileIdx = UINT64(-1);
        for (UINT64 i = 0; i < numProfiles; ++i)
        {
            if (profiles[i].name == name)
            {
                profileIdx = i;
                break;
            }
        }

        if (profileIdx == UINT64(-1))
        {
            profileIdx = numProfiles++;
            profiles[profileIdx].name = name;
        }

        seedAssert(profileIdx != UINT64(-1));
        seedAssert(profileIdx < maxProfiles);

        ProfileData& profileData = profiles[profileIdx];
        profileData.Active = true;

        // Insert the start timestamp
        const uint startQueryIdx = uint(profileIdx * 2);
        cb.cmd->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, startQueryIdx);

        profileData.QueryStarted = true;

        cb.profileIdx = (uint)profileIdx;
        return profileIdx;
    }

    void EndProfile(CommandBuffer& cb)
    {
        ZoneScoped;
        ProfileData& profileData = profiles[cb.profileIdx];

        // Insert the end timestamp
        const uint startQueryIdx = uint(cb.profileIdx * QUEUECOUNT);
        const uint endQueryIdx = startQueryIdx + 1;
        cb.cmd->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, endQueryIdx);

        // Resolve the data
        const UINT64 dstOffset = ((GPU::instance->frameIndex * maxProfiles * QUEUECOUNT) + startQueryIdx) * sizeof(UINT64);
        cb.cmd->ResolveQueryData(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, startQueryIdx, 2, buffer.GetResource(), dstOffset);
    }

    void UpdateProfile(ProfileData& profile, UINT64 profileIdx, UINT64 gpuFrequency, const UINT64* frameQueryData)
    {
        ZoneScoped;
        profile.QueryFinished = false;

        double time = 0.0f;
        UINT64 startTime = 0;
        UINT64 endTime = 0;

        if (profile.CPUProfile)
        {
            time = double(profile.EndTime - profile.StartTime) / 1000.0;
        }
        else if (frameQueryData)
        {
            // Get the query data
            startTime = frameQueryData[profileIdx * 2 + 0];
            endTime = frameQueryData[profileIdx * 2 + 1];

            if (endTime > startTime)
            {
                UINT64 delta = endTime - startTime;
                double frequency = double(gpuFrequency);
                time = (delta / frequency) * 1000.0;
            }
        }

        profile.TimeSamples[profile.CurrSample] = time;
        profile.CurrSample = (profile.CurrSample + 1) % ProfileData::FilterSize;

        profile.Active = false;
    }

    void EndProfilerFrame(CommandBuffer& cb)
    {
        ZoneScoped;
        UINT64 gpuFrequency = 0;
        const UINT64* frameQueryData = nullptr;

        cb.queue->GetTimestampFrequency(&gpuFrequency);

        readbackBuffer.ReadBack(buffer, cb);

        UINT64* queryData = NULL;
        ID3D12Resource* res = readbackBuffer.gpuData.GetResource();
        res->Map(0, nullptr, (void**)&queryData);
        frameQueryData = queryData + (GPU::instance->frameIndex * maxProfiles * 2);
        res->Unmap(0, nullptr);

        // Iterate over all of the profiles
        for (UINT64 profileIdx = 0; profileIdx < numProfiles; ++profileIdx)
            UpdateProfile(profiles[profileIdx], profileIdx, gpuFrequency, frameQueryData);
    }

    void Off()
    {
        ZoneScoped;
        buffer.Release();
        readbackBuffer.Release();
        queryHeap->Release();
    }
};
Profiler* Profiler::instance;
