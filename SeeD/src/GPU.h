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


#define FRAMEBUFFERING 2

struct Fence
{
    ID3D12Fence* fence = NULL;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent;
};

struct CommandBuffer
{
    // everything is a graphic command, why no compute list ?
    ID3D12GraphicsCommandList6* cmd = NULL;
    ID3D12CommandAllocator* cmdAlloc = NULL;
    ID3D12CommandQueue* queue = NULL;
    Fence passEnd;
    Fence waitFor;
    uint profileIdx;
    bool open;
};

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

// c´est crade, c´est parceque GPU est pas encore definit et j´ai pas acces a GPU::frameIndex;
uint g_frameIndex;
template<class T>
class PerFrame
{
    T data[FRAMEBUFFERING];
public:
    T* Get(uint frameIndex)
    {
        return &data[frameIndex];
    }
    T* Get()
    {
        return Get(g_frameIndex);
    }
    T* operator -> ()
    {
        return Get();
    }
};


// FUCK this the first forward declaration I need to make :(

class Resource
{
public:
    D3D12MA::Allocation* allocation{};
    uint stride{};
    SRV srv{ UINT32_MAX };
    UAV uav{ UINT32_MAX };
    RTV rtv{ UINT32_MAX, 0, 0 };

    void CreateTexture(uint2 resolution, String name = "Texture");
    void CreateBuffer(uint size, uint stride, bool upload = false, String name = "Buffer");
    template <typename T>
    void CreateBuffer(uint count, String name = "Buffer");
    template <typename T>
    void CreateUploadBuffer(uint count, String name = "Buffer");
    void CreateReadBackBuffer(uint size, String name = "ReadBackBuffer");
    void BackBuffer(ID3D12Resource* backBuffer);
    void Release();
    ID3D12Resource* GetResource();
    void Upload(void* data, CommandBuffer* cb);
    void Transition(CommandBuffer* cb, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);
    static void CleanUploadResources();
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

    void On(ID3D12Device9* device)
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

        //descriptorDSVIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
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
};

class GPU
{
public:
    static GPU* instance;
    IDXGIFactory4* dxgiFactory{};
    IDXGIAdapter3* adapter{};
    ID3D12Device9* device{};

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

    ID3D12CommandSignature* commandSignature; // just for the drawIndirect ?!

    uint frameIndex{};
    uint frameNumber{};

    void On(IOs::WindowInformation* window)
    {
        instance = this;
        ZoneScoped;
        CreateDevice(window);
        descriptorHeap.On(device);
        CreateSwapChain(window);
        CreateMemoryAllocator();

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

            hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, _uuidof(ID3D12Device9), nullptr);
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
        swapChainDescFullscreen.Windowed = !window->fullScreen;

        IDXGISwapChain1* tempSwapChain;

        hr = dxgiFactory->CreateSwapChainForHwnd(
            graphicQueue, // the queue will be flushed once the swap chain is created
            window->windowHandle,
            &swapChainDesc, // give it the swap chain description we created above
            window->fullScreen ? &swapChainDescFullscreen : nullptr,
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
            backBuffer.Get(i)->BackBuffer(res);
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
                backBuffer.Get(i)->BackBuffer(res);
            }
        }
    }

    void Off()
    {
        ZoneScoped;
        graphicQueue->Release();
        computeQueue->Release();
        allocator->Release();
        //swapChain->Release();
        device->Release();
        adapter->Release();
        dxgiFactory->Release();
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
void Resource::CreateTexture(uint2 resolution, String name)
{
    //ZoneScoped;
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = resolution.x;
    resourceDesc.Height = resolution.y;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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

    allocation->SetName(name.ToConstWChar());
    allResources.push_back(allocation);
    allResourcesNames.push_back(name);

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
        allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        NULL,
        &allocation,
        IID_NULL, NULL);

    allocation->SetName(name.ToConstWChar());
    allResources.push_back(allocation);
    allResourcesNames.push_back(name);

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(srv);

        auto desc = GetResource()->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Buffer = {};
        SRVDesc.Buffer.FirstElement = 0;
        SRVDesc.Buffer.NumElements = (UINT)(desc.Width / stride);
        SRVDesc.Buffer.StructureByteStride = (UINT)stride;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        GPU::instance->device->CreateShaderResourceView(GetResource(), &SRVDesc, handle);
    }

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(uav);

        auto desc = GetResource()->GetDesc();
        D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        UAVDesc.Buffer = {};
        UAVDesc.Buffer.FirstElement = 0;
        UAVDesc.Buffer.NumElements = (UINT)(desc.Width / stride);
        UAVDesc.Buffer.StructureByteStride = (UINT)stride;
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
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

    allocation->SetName(name.ToConstWChar());
    allResources.push_back(allocation);
    allResourcesNames.push_back(name);

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
    if(allocation) allocation->Release();
}

ID3D12Resource* Resource::GetResource()
{
    if (rtv.raw)
        return rtv.raw;
    if(allocation)
        return allocation->GetResource();
    return nullptr;
}

void Resource::Upload(void* data, CommandBuffer* cb)
{
    //ZoneScoped;
    D3D12MA::Allocation* uploadAllocation;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = allocation->GetSize();
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

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
    uploadResources.push_back({ GPU::instance->frameNumber, uploadAllocation });
    allResources.push_back(uploadAllocation);
    allResourcesNames.push_back("UploadBuffer");

    void* buf;
    uploadAllocation->GetResource()->Map(0, nullptr, &buf);
    memcpy(buf, data, uploadAllocation->GetSize());
    uploadAllocation->GetResource()->Unmap(0, nullptr);

    Transition(cb, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cb->cmd->CopyResource(allocation->GetResource(), uploadAllocation->GetResource());
    Transition(cb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
}

void Resource::Transition(CommandBuffer* cb, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{
    D3D12_RESOURCE_BARRIER trans{ D3D12_RESOURCE_BARRIER_TYPE_TRANSITION , D3D12_RESOURCE_BARRIER_FLAG_NONE , {GetResource(), 0, stateBefore, stateAfter} };
    cb->cmd->ResourceBarrier(1, &trans);
}

void Resource::CleanUploadResources()
{
    //ZoneScoped;
    uint frameNumberThreshold = GPU::instance->frameNumber - 2;
    for (int i = (int)uploadResources.size() - 1; i >= 0 ; i--)
    {
        if (std::get<0>(uploadResources[i]) < frameNumberThreshold)
        {
            D3D12MA::Allocation* alloc = std::get<1>(uploadResources[i]);
            for (int j = (int)allResources.size() - 1; j >= 0; j--)
            {
                if (allResources[j] == alloc)
                {
                    allResources.erase(allResources.begin() + i);
                    allResourcesNames.erase(allResourcesNames.begin() + i);
                }
            }
            alloc->Release();
            uploadResources.erase(uploadResources.begin() + i);
        }
    }
}

std::vector<std::tuple<uint, D3D12MA::Allocation*>> Resource::uploadResources;
std::vector<D3D12MA::Allocation*> Resource::allResources;
std::vector<String> Resource::allResourcesNames;
// end of definitions of Resource::

template <class T>
class StructuredUploadBuffer
{
public:
    std::vector<T> cpuData;
    Resource gpuData;

    void Clear()
    {
        cpuData.clear();
    }
    uint Add(T value)
    {
        cpuData.push_back(value);
        return (uint)cpuData.size();
    }
    uint AddUnique(T value)
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
    uint Size()
    {
        return cpuData.size();
    }
    Resource& GetResource()
    {
        return gpuData;
    }
    ID3D12Resource* GetResourcePtr()
    {
        return gpuData.GetResource();
    }
    void Upload()
    {
        ZoneScoped;
        if (gpuData.allocation == nullptr || gpuData.allocation->GetSize() < cpuData.size * sizeof(T))
        {
            gpuData.CreateUploadBuffer<T>(cpuData.size, typeid(T).name());
        }
        void* buf;
        GetResourcePtr()->Map(0, nullptr, &buf);
        memcpy(buf, cpuData.data, sizeof(T) * cpuData.size());
        GetResourcePtr()->Unmap(0, nullptr);
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
    void ReadBack(Resource& resource, CommandBuffer* cb)
    {
        ZoneScoped;
        if (gpuData.GetResource() == nullptr)
        {
            // can´t use resource.allocation->GetSize().... it give the entire DMA page size ?
            auto descsource = resource.GetResource()->GetDesc();
            gpuData.CreateReadBackBuffer((uint)descsource.Width);
        }
        resource.Transition(cb, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cb->cmd->CopyResource(GetResourcePtr(), resource.GetResource());
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

struct BLAS
{

};

struct TLAS
{
    ~TLAS()
    {

    }
};

struct Camera
{

};

struct Light
{

};

struct Instance
{
    uint meshIndex;
    uint materialIndex;
    float4x4 worldMatrix;
};

struct Meshlet
{
    /* offsets within meshlet_vertices and meshlet_triangles arrays with meshlet data */
    uint vertexOffset;
    uint triangleOffset;

    /* number of vertices and triangles used in the meshlet; data is stored in consecutive range defined by offset and count */
    uint vertexCount;
    uint triangleCount;
};

struct Mesh
{
    uint meshOffset;
    uint meshletCount;
    BLAS blas;
};

struct Material
{
    uint shaderIndex;
    float parameters[15];
    SRV texturesSRV[16];
};

struct PSO
{

};

struct ShaderCode
{
    PSO pso;
};

struct Shader
{
    ShaderCode shaderCode;
    std::map<String, __time64_t> creationTime;
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

        assert(profileIdx != UINT64(-1));
        assert(profileIdx < maxProfiles);

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

    void EndProfilerFrame(CommandBuffer* cb)
    {
        ZoneScoped;
        UINT64 gpuFrequency = 0;
        const UINT64* frameQueryData = nullptr;

        cb->queue->GetTimestampFrequency(&gpuFrequency);

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
