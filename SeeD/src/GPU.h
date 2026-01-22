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

        // Helper to compute aligned buffer sizes
#ifndef ROUND_UP
#define ROUND_UP(v, powerOf2Alignment)                                         \
  (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))
#endif

struct SRV
{
    uint offset;
    D3D12_GPU_DESCRIPTOR_HANDLE handle;
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

struct CommandBuffer;
class Resource
{
public:
    D3D12MA::Allocation* allocation{ 0 };
    uint stride{ 0 };
    SRV srv{ UINT32_MAX };
    UAV uav{ UINT32_MAX };
    RTV rtv{ UINT32_MAX, 0, 0 };
    DSV dsv{ UINT32_MAX, 0 };

    void Create(D3D12_RESOURCE_DESC desc, String name = "Texture");
    void CreateTexture(uint2 resolution, DXGI_FORMAT format, bool mips, String name = "Texture");
    void CreateTexture(uint3 resolution, DXGI_FORMAT format, bool mips, String name = "Texture");
    void CreateRenderTarget(uint2 resolution, DXGI_FORMAT format, String name = "Texture");
    void CreateDepthTarget(uint2 resolution, String name = "Texture");
    void CreateBuffer(uint size, uint stride, bool upload = false, String name = "Buffer", D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);
    template <typename T>
    void CreateBuffer(uint count, String name = "Buffer");
    template <typename T>
    void CreateUploadBuffer(uint count, String name = "UploadBuffer");
    template <typename T>
    void CreateAppendBuffer(uint count, String name = "AppendBuffer");
    void CreateReadBackBuffer(uint size, String name = "ReadBackBuffer");
    void CreateAccelerationStructure(uint size, String name = "AccelerationStructure");
    void BackBuffer(ID3D12Resource* backBuffer);
    void Release(bool deferred = false);
    ID3D12Resource* GetResource();
    uint BufferSize();
    void UploadTexture(std::vector<D3D12_SUBRESOURCE_DATA> subresources, CommandBuffer& cb);
    void Upload(void* data, uint dataSize, uint offset, CommandBuffer& cb);
    void UploadElements(void* data, uint count, uint index, CommandBuffer& cb);
    void Transition(CommandBuffer& cb, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);
    void Transition(CommandBuffer& cb, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter, ID3D12Resource* resource);
    void Barrier(CommandBuffer& cb);
    uint AlignForUavCounter(uint bufferSize)
    {
        const uint alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
        return (bufferSize + (alignment - 1)) & ~(alignment - 1);
    }
    uint StrideForStructuredBuffer(uint stride)
    {
        const uint alignment = D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;
        return (stride + (alignment - 1)) & ~(alignment - 1);
    }
    static void ReleaseResources(bool everything = false);

    static std::mutex lock;
    static std::vector<Resource> allResources;
    static std::vector<String> allResourcesNames;
    static std::vector<std::pair<uint, Resource>> releaseResources;
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
        if (HLSL::reverseZ) ds.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
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
    enum class Type
    {
        Graphic,
        Mesh,
        Compute,
        Raytracing
    } type;

    ID3D12CommandSignature* commandSignature = 0;
    ID3D12RootSignature* rootSignature = 0;
    ID3D12PipelineState* pso = 0;
    D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    // read those from shader reflection
    uint3 numthreads;
    std::vector<DXGI_FORMAT> outputs;

    // for raytracing
    ID3D12StateObject* rtStateObject;
    ID3D12StateObjectProperties* rtStateObjectProps;
    //StructuredUploadBuffer<byte> shaderBindingTable;
    Resource shaderBindingTable;
    //std::vector<std::pair<String, std::vector<void*>>> entries;
     // no parameters, just have one global root sig ?
    std::vector<String> rayGen;
    std::vector<String> miss;
    std::vector<String> hit;
    std::vector<String> hitGroup;
    static constexpr uint progIdSize = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

    std::map<String, __time64_t> creationTime;
    bool NeedReload()
    {
        if (options.shaderReload)
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

    uint DispatchX(uint count)
    {
        return uint(ceil(float(count) / float(numthreads.x)));
    }

    uint DispatchY(uint count)
    {
        return uint(ceil(float(count) / float(numthreads.y)));
    }

    uint DispatchZ(uint count)
    {
        return uint(ceil(float(count) / float(numthreads.z)));
    }

    D3D12_DISPATCH_RAYS_DESC GetRTDesc()
    {
        uint rayGenSize = ROUND_UP(progIdSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        uint missSize = ROUND_UP(progIdSize * miss.size(), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        uint hitGroupSize = ROUND_UP(progIdSize * hitGroup.size(), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

        uint bufferSize = rayGenSize + missSize + hitGroupSize;
        bufferSize *= 100;
        if (shaderBindingTable.GetResource() == nullptr || shaderBindingTable.GetResource()->GetDesc().Width < bufferSize)
        {
            shaderBindingTable.Release();
            shaderBindingTable.CreateBuffer(bufferSize, 4, true, "ShaderBindingTable", D3D12_RESOURCE_STATE_COMMON);
        }

        D3D12_DISPATCH_RAYS_DESC drd = {};

        drd.Depth = 1;

        drd.RayGenerationShaderRecord.StartAddress = shaderBindingTable.GetResource()->GetGPUVirtualAddress();
        drd.RayGenerationShaderRecord.SizeInBytes = rayGenSize;

        drd.MissShaderTable.StartAddress = drd.RayGenerationShaderRecord.StartAddress + drd.RayGenerationShaderRecord.SizeInBytes;
        drd.MissShaderTable.SizeInBytes = missSize;
        drd.MissShaderTable.StrideInBytes = progIdSize;

        drd.HitGroupTable.StartAddress = drd.MissShaderTable.StartAddress + drd.MissShaderTable.SizeInBytes;
        drd.HitGroupTable.SizeInBytes = hitGroupSize;
        drd.HitGroupTable.StrideInBytes = progIdSize;

        UINT64 shaderBindingTableSize = drd.RayGenerationShaderRecord.SizeInBytes + drd.MissShaderTable.SizeInBytes + drd.HitGroupTable.SizeInBytes;
        //seedAssert(bufferSize == shaderBindingTableSize);

        uint8_t* sbt;
        shaderBindingTable.GetResource()->Map(0, nullptr, (void**)&sbt);

        CopyRTShaderData(sbt, rayGen);
        sbt += drd.RayGenerationShaderRecord.SizeInBytes;

        CopyRTShaderData(sbt, miss);
        sbt += drd.MissShaderTable.SizeInBytes;

        CopyRTShaderData(sbt, hitGroup);

        shaderBindingTable.GetResource()->Unmap(0, nullptr);

        return drd;
    }
    uint CopyRTShaderData(uint8_t* outputData, const std::vector<String>& shaders)
    {
        uint8_t* pData = outputData;
        for (const auto& shader : shaders)
        {
            // Get the shader identifier, and check whether that identifier is known
            void* id = rtStateObjectProps->GetShaderIdentifier(shader.ToWString().c_str());
            if (!id)
            {
                IOs::Log("Unknown shader identifier used in the SBT: {}", shader.c_str());
                seedAssert(!id);
            }
            // Copy the shader identifier
            memcpy(pData, id, progIdSize);
            /*
            // Copy all its resources pointers or values in bulk
            memcpy(pData + progIdSize, shader.m_inputData.data(), shader.m_inputData.size() * 8);
            */

            pData += progIdSize;
        }
        // Return the number of bytes actually written to the output buffer
        return 0;
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
    uint profileIdx;
    bool open;

    void SetCompute(Shader& shader);
    void SetRaytracing(Shader& shader);
    void SetGraphic(Shader& shader);
    void On(ID3D12CommandQueue* queue, String name);
    void Off();
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

    std::mutex lock;

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
        /*
        // do we ?!
        if (*offset != UINT32_MAX)
        {
            slots->Release(*offset);
        }
        */

        lock.lock();
        *offset = (uint)slots->Get();
        lock.unlock();
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

    void FreeSlot(uint* offset, Slots* slots, ID3D12DescriptorHeap* heap, int descriptorIncrementSize)
    {
        if (*offset != UINT32_MAX)
        {
            lock.lock();
            slots->Release(*offset);
            lock.unlock();
        }
        *offset = UINT32_MAX;
    }
    void FreeGlobalSlot(SRV& srv)
    {
        FreeSlot(&srv.offset, &globalDescriptorHeapSlots, globalDescriptorHeap, descriptorIncrementSize);
    }
    void FreeGlobalSlot(UAV& uav)
    {
        FreeSlot(&uav.offset, &globalDescriptorHeapSlots, globalDescriptorHeap, descriptorIncrementSize);
    }
    void FreeRTVSlot(RTV& rtv)
    {
        FreeSlot(&rtv.offset, &rtvDescriptorHeapSlots, rtvDescriptorHeap, rtvdescriptorIncrementSize);
    }
    void FreeDSVSlot(DSV& dsv)
    {
        FreeSlot(&dsv.offset, &dsvDescriptorHeapSlots, dsvDescriptorHeap, dsvdescriptorIncrementSize);
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
    T& GetPrevious()
    {
        return Get((g_frameIndex + 1) % FRAMEBUFFERING);
    }
    T* operator -> ()
    {
        return &Get();
    }
};

static PerFrame<CommandBuffer>* endOfLastFrame = nullptr;

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
    ID3D12CommandQueue* copyQueue{};

    IDXGISwapChain3* swapChain{};

    PerFrame<Resource> backBuffer;

    D3D12MA::Allocator* allocator{};

    DescriptorHeap descriptorHeap;

    // for the perFrame stuff
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
        copyQueue->Release();

        Resource::ReleaseResources(true);
        //Resource::CleanUploadResources(true);
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
        commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&graphicQueue));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            return;
        }
        graphicQueue->SetName(L"graphicQueue");

        commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_COMPUTE;
        hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&computeQueue));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            return;
        }
        computeQueue->SetName(L"computeQueue");

        commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_COPY;
        hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&copyQueue));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            return;
        }
        copyQueue->SetName(L"copyQueue");
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
        swapChainDesc.Stereo = FALSE;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
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


void Resource::Create(D3D12_RESOURCE_DESC resourceDesc, String name)
{
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
    allResources.push_back(*this);
    allResourcesNames.push_back(name);
    lock.unlock();

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        auto desc = GetResource()->GetDesc();
        srv.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));
        srv.handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = desc.Format;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MipLevels = desc.MipLevels;
        GPU::instance->device->CreateShaderResourceView(GetResource(), &SRVDesc, handle);
    }

    //exclude BC format for UAV
    if (!((resourceDesc.Format >= DXGI_FORMAT_BC1_TYPELESS && resourceDesc.Format <= DXGI_FORMAT_BC5_SNORM)
        || (resourceDesc.Format >= DXGI_FORMAT_BC6H_TYPELESS && resourceDesc.Format <= DXGI_FORMAT_BC7_UNORM_SRGB)
        || !(resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)))
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

// definitions of Resource
void Resource::CreateTexture(uint2 resolution, DXGI_FORMAT format, bool mips, String name)
{
    CreateTexture(uint3(resolution.x, resolution.y, 1), format, mips, name);
}

void Resource::CreateTexture(uint3 resolution, DXGI_FORMAT format, bool mips, String name)
{ 
    //ZoneScoped;
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = resolution.z > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = resolution.x;
    resourceDesc.Height = resolution.y;
    resourceDesc.DepthOrArraySize = resolution.z;
    resourceDesc.MipLevels = mips ? 10 : 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

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
    allResources.push_back(*this);
    allResourcesNames.push_back(name);
    lock.unlock();

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        auto desc = GetResource()->GetDesc();
        srv.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));
        srv.handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_SRV_DIMENSION dim = resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? D3D12_SRV_DIMENSION_TEXTURE3D : D3D12_SRV_DIMENSION_TEXTURE2D;

        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = desc.Format;
        SRVDesc.ViewDimension = dim;
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
        if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            UAVDesc.Texture2D.MipSlice = 0;
        }
        else if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        {
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            UAVDesc.Texture3D.MipSlice = 0;
            UAVDesc.Texture3D.FirstWSlice = 0;
            UAVDesc.Texture3D.WSize = resolution.z;
        }
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
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = resourceDesc.Format;
    clearValue.Color[0] = 0;
    clearValue.Color[1] = 0;
    clearValue.Color[2] = 0;
    clearValue.Color[3] = 0;

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        &allocation,
        IID_NULL, NULL);

    std::wstring wname = name.ToWString();
    allocation->SetName(wname.c_str());
    allocation->GetResource()->SetName(wname.c_str());
    lock.lock();
    allResources.push_back(*this);
    allResourcesNames.push_back(name);
    lock.unlock();

    {
        auto desc = GetResource()->GetDesc();
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        srv.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));
        srv.handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = desc.Format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT : desc.Format;
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
        UAVDesc.Format = desc.Format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT : desc.Format;
        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        UAVDesc.Texture2D.MipSlice = 0;
        GPU::instance->device->CreateUnorderedAccessView(GetResource(), nullptr, &UAVDesc, handle);
    }

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
    if (HLSL::reverseZ) clear.DepthStencil = { 0,0 };
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
    allResources.push_back(*this);
    allResourcesNames.push_back(name);
    lock.unlock();

    {
        auto desc = GetResource()->GetDesc();
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        srv.offset = (uint)GPU::instance->descriptorHeap.globalDescriptorHeapSlots.Get();
        handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));
        srv.handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

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

void Resource::CreateBuffer(uint size, uint _stride, bool upload, String name, D3D12_RESOURCE_STATES initialState)
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

    D3D12_RESOURCE_ALLOCATION_INFO1 allocInfo;
    GPU::instance->device->GetResourceAllocationInfo1(0, 1, &resourceDesc, &allocInfo);

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    if (upload)
    {
        allocationDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    }

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        initialState,
        NULL,
        &allocation,
        IID_NULL, NULL);
    if (FAILED(hr))
    {
        IOs::Log("Buffer {} creation failed {}", name.c_str(), hr);
    }

    std::wstring wname = name.ToWString();
    allocation->SetName(wname.c_str());
    allocation->GetResource()->SetName(wname.c_str());
    lock.lock();
    allResources.push_back(*this);
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
    uint stride = sizeof(T);
    CreateBuffer(stride * count, stride, false, name);
}
template <typename T>
void Resource::CreateUploadBuffer(uint count, String name)
{
    uint stride = sizeof(T);
    CreateBuffer(stride * count, stride, true, name);
}
template <typename T>
void Resource::CreateAppendBuffer(uint count, String name)
{
    uint size = AlignForUavCounter(sizeof(T) * count);
    CreateBuffer(size, sizeof(T), false, name);
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
    allResources.push_back(*this);
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

void Resource::CreateAccelerationStructure(uint size, String name)
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
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        NULL,
        &allocation,
        IID_NULL, NULL);
    if (FAILED(hr))
    {
        GPU::PrintDeviceRemovedReason(hr);
        return;
    }

    std::wstring wname = name.ToWString();
    allocation->SetName(wname.c_str());
    lock.lock();
    allResources.push_back(*this);
    allResourcesNames.push_back(name);
    lock.unlock();

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GPU::instance->descriptorHeap.GetGlobalSlot(srv);

        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRVDesc.Format = resourceDesc.Format;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        SRVDesc.RaytracingAccelerationStructure.Location = allocation->GetResource()->GetGPUVirtualAddress();
        GPU::instance->device->CreateShaderResourceView(nullptr, &SRVDesc, handle);
    }

    /*
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
        srv.handle.ptr = static_cast<SIZE_T>(GPU::instance->descriptorHeap.globalDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + INT64(srv.offset) * UINT64(GPU::instance->descriptorHeap.descriptorIncrementSize));

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

void Resource::Release(bool deferred)
{
    if (allocation)
    {
        if (deferred)
        {
            lock.lock();
            for (int j = (int)allResources.size() - 1; j >= 0; j--)
            {
                if (allResources[j].allocation == this->allocation)
                {
                    allResources.erase(allResources.begin() + j);
                    allResourcesNames.erase(allResourcesNames.begin() + j);
                }
            }
            releaseResources.push_back({ GPU::instance->frameNumber , *this });
            lock.unlock();
        }
        else
        {
            lock.lock();
            for (int j = (int)allResources.size() - 1; j >= 0; j--)
            {
                if (allResources[j].allocation == this->allocation)
                {
                    allResources.erase(allResources.begin() + j);
                    allResourcesNames.erase(allResourcesNames.begin() + j);
                }
            }
            lock.unlock();
            allocation->Release();
            GPU::instance->descriptorHeap.FreeGlobalSlot(srv);
            GPU::instance->descriptorHeap.FreeGlobalSlot(uav);
            GPU::instance->descriptorHeap.FreeRTVSlot(rtv);
            GPU::instance->descriptorHeap.FreeDSVSlot(dsv);
        }
    }
    allocation = { 0 };
    stride = { 0 };
    srv = { UINT32_MAX };
    uav = { UINT32_MAX };
    rtv = { UINT32_MAX, 0, 0 };
    dsv = { UINT32_MAX, 0 };
}

ID3D12Resource* Resource::GetResource()
{
    if (rtv.raw)
        return rtv.raw;
    if (allocation)
        return allocation->GetResource();
    return nullptr;
}

uint Resource::BufferSize()
{
    return (uint)GetResource()->GetDesc().Width;
}

void Resource::UploadTexture(std::vector<D3D12_SUBRESOURCE_DATA> subresources, CommandBuffer& cb)
{

    //UINT64 uploadBufferSize = GPU::instance->device->GetCopyableFootprints(&GetResource()->GetDesc(), 0, subresources.size(), 0, nullptr, nullptr, nullptr, &uploadBufferSize);

    D3D12MA::Allocation* uploadAllocation;

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = GetResource()->GetDesc();

    HRESULT hr = GPU::instance->allocator->CreateResource(
        &allocationDesc,
        &desc,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        NULL,
        &uploadAllocation,
        IID_NULL, NULL);

    Resource* tmpUpRes = new Resource();
    tmpUpRes->allocation = uploadAllocation;

    /*
    uploadAllocation->SetName(L"UploadTexture");
    lock.lock();
    releaseResources.push_back({ GPU::instance->frameNumber, *tmpUpRes });
    lock.unlock();

    void* buf;
    uploadAllocation->GetResource()->Map(0, nullptr, &buf);
    memcpy(buf, data, dataSize);
    uploadAllocation->GetResource()->Unmap(0, nullptr);

    BYTE* pData;
    HRESULT hr = tmpUpRes->GetResource()->Map(0, nullptr, reinterpret_cast<void**>(&pData));
    if (FAILED(hr))
    {
        return;
    }
    for (UINT i = 0; i < NumSubresources; ++i)
    {
        if (pRowSizesInBytes[i] > SIZE_T(-1)) return 0;
        D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, SIZE_T(pLayouts[i].Footprint.RowPitch) * SIZE_T(pNumRows[i]) };
        MemcpySubresource(&DestData, &pSrcData[i], static_cast<SIZE_T>(pRowSizesInBytes[i]), pNumRows[i], pLayouts[i].Footprint.Depth);
    }
    pIntermediate->Unmap(0, nullptr);

    lock.lock();
    Transition(cb, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cb.cmd->CopyTextureRegion(GetResource(), offset, uploadAllocation->GetResource(), 0, dataSize);
    Transition(cb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    lock.unlock();
    */
}

void Resource::Upload(void* data, uint dataSize, uint offset, CommandBuffer& cb)
{
    //ZoneScoped;
    if (options.stopBufferUpload)
        return;
    //ZoneScopedN("Resource::Upload");

    if (dataSize + offset > GetResource()->GetDesc().Width)
        IOs::Log("Resource {} full", WCharToString(allocation->GetName()));

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

    Resource* tmpUpRes = new Resource();
    tmpUpRes->allocation = uploadAllocation;

    uploadAllocation->SetName(L"UploadBuffer");
    lock.lock();
    releaseResources.push_back({ GPU::instance->frameNumber, *tmpUpRes });
    lock.unlock();

    void* buf;
    uploadAllocation->GetResource()->Map(0, nullptr, &buf);
    memcpy(buf, data, dataSize);
    uploadAllocation->GetResource()->Unmap(0, nullptr);

    lock.lock();
    cb.cmd->CopyBufferRegion(allocation->GetResource(), offset, uploadAllocation->GetResource(), 0, dataSize);
    lock.unlock();
}

void Resource::UploadElements(void* data, uint count, uint index, CommandBuffer& cb)
{
    Upload(data, count * stride, index * stride, cb);
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

void Resource::Barrier(CommandBuffer& cb)
{
    D3D12_RESOURCE_BARRIER trans{ D3D12_RESOURCE_BARRIER_TYPE_UAV , D3D12_RESOURCE_BARRIER_FLAG_NONE , {GetResource(), 0, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON} };
    cb.cmd->ResourceBarrier(1, &trans);
}

void Resource::ReleaseResources(bool everything)
{
    //ZoneScoped; 
    uint frameNumberThreshold = GPU::instance->frameNumber - 1;
    if (everything)
        frameNumberThreshold = UINT_MAX;
    for (int i = (int)releaseResources.size() - 1; i >= 0; i--)
    {
        if (releaseResources[i].first < frameNumberThreshold)
        {
            Resource& res = releaseResources[i].second;
            res.Release();
            releaseResources.erase(releaseResources.begin() + i);
        }
    }
}

std::mutex Resource::lock;
std::vector<Resource> Resource::allResources;
std::vector<String> Resource::allResourcesNames;
std::vector< std::pair<uint, Resource>> Resource::releaseResources;
// end of definitions of Resource

// definitions of CommandBuffer      
void CommandBuffer::SetCompute(Shader& shader)
{
    // SetDescriptorHeaps must be set before SetPipelineState because of dynamic indexing in descriptorHeap
    cmd->SetDescriptorHeaps(1, &GPU::instance->descriptorHeap.globalDescriptorHeap);
    cmd->SetComputeRootSignature(shader.rootSignature);
    cmd->SetPipelineState(shader.pso);
    //cmd->SetGraphicsRootDescriptorTable();
}
void CommandBuffer::SetRaytracing(Shader& shader)
{
    // SetDescriptorHeaps must be set before SetPipelineState because of dynamic indexing in descriptorHeap
    cmd->SetDescriptorHeaps(1, &GPU::instance->descriptorHeap.globalDescriptorHeap);
    cmd->SetComputeRootSignature(shader.rootSignature);
    cmd->SetPipelineState1(shader.rtStateObject);
}
void CommandBuffer::SetGraphic(Shader& shader)
{
    // SetDescriptorHeaps must be set before SetPipelineState because of dynamic indexing in descriptorHeap
    cmd->SetDescriptorHeaps(1, &GPU::instance->descriptorHeap.globalDescriptorHeap);
    cmd->SetGraphicsRootSignature(shader.rootSignature);
    cmd->SetPipelineState(shader.pso);
    cmd->IASetPrimitiveTopology(shader.primitiveTopology);
    //cmd->SetGraphicsRootDescriptorTable();
}
void CommandBuffer::On(ID3D12CommandQueue* _queue, String name)
{
    queue = _queue;
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (queue == GPU::instance->computeQueue) type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    if (queue == GPU::instance->copyQueue) type = D3D12_COMMAND_LIST_TYPE_COPY;
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
    hr = cmd->SetName(wname.c_str());
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
    passEnd.fence->SetName(wname.c_str());
    if (FAILED(hr))
    {
        GPU::PrintDeviceRemovedReason(hr);
    }
    passEnd.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, wname.c_str());
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

template <class T>
class StructuredBuffer
{
public:
    Resource gpuData;
    Resource readbackgpuData;

    void CreateBuffer(uint elementCount, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON)
    {
        gpuData.Release(true);
        elementCount = max(uint1(1), uint1(elementCount));
        gpuData.CreateBuffer(sizeof(T) * elementCount, sizeof(T), false, typeid(T).name(), initialState);
    }

    void Release()
    {
        gpuData.Release();
        readbackgpuData.Release();
    }

    uint Size()
    {
        if (gpuData.GetResource() != nullptr)
        {
            return (uint)gpuData.GetResource()->GetDesc().Width / sizeof(T);
        }
        return 0;
    }

    void Resize(uint elementCount)
    {
        if (elementCount > 0 && Size() < elementCount)
        {
            CreateBuffer(elementCount);
        }
    }

    Resource& GetResource()
    {
        return gpuData;
    }
    ID3D12Resource* GetResourcePtr()
    {
        return gpuData.GetResource();
    }

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress(uint index = 0)
    {
        if (gpuData.GetResource() == 0)
            return 0;
        return gpuData.GetResource()->GetGPUVirtualAddress() + (index * sizeof(T));
    }

    void ReadBack(CommandBuffer& cb)
    {
        ZoneScoped;
        if (readbackgpuData.GetResource() == nullptr)
        {
            // can�t use resource.allocation->GetSize().... it give the entire DMA page size ?
            auto descsource = gpuData.GetResource()->GetDesc();
            readbackgpuData.CreateReadBackBuffer((uint)descsource.Width);
        }
        gpuData.Transition(cb, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cb.cmd->CopyResource(readbackgpuData.GetResource(), gpuData.GetResource());
        gpuData.Transition(cb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    }

    void ReadBackMap(void** data)
    {
        readbackgpuData.GetResource()->Map(0, nullptr, data);
    }

    void ReadBackUnMap()
    {
        readbackgpuData.GetResource()->Unmap(0, nullptr);
    }

};

template <class T>
class StructuredUploadBuffer : public StructuredBuffer<T>
{
public:
    std::mutex lock;
    std::vector<T> cpuData;

    void Clear()
    {
        cpuData.clear();
    }
    uint Add(const T& value)
    {
        lock.lock();
        cpuData.push_back(value);
        uint index = (uint)cpuData.size() - 1;
        lock.unlock();
        return index;
    }
    uint AddUnique(const T& value)
    {
        lock.lock();
        uint index = 0;
        for (; index < cpuData.size(); index++)
        {
            if (memcmp(&cpuData[index], &value, sizeof(T)) == 0)
                return index;
        }
        cpuData.push_back(value);
        lock.unlock();
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
        lock.unlock();
    }
    void AddRangeWithTransform(T* data, uint count, void (*f)(int, T&))
    {
        lock.lock();
        cpuData.reserve(cpuData.size() + count);
        for (uint i = 0; i < count; i++)
        {
            f((int)cpuData.size(), data[i]);
            cpuData.push_back(data[i]);
        }
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
    T* Data()
    {
        return cpuData.data();
    }
    void Upload()
    {
        ZoneScoped;
        if (options.stopBufferUpload)
            return;
        if (cpuData.size() <= 0)
            return;
        if (this->gpuData.allocation == nullptr || this->gpuData.BufferSize() <= (cpuData.size() * sizeof(T)))
        {
            this->gpuData.Release(true);
            this->gpuData.CreateUploadBuffer<T>(max(uint1(cpuData.size()), uint1(1)), typeid(T).name());
        }
        void* buf;
        auto hr = this->GetResourcePtr()->Map(0, nullptr, &buf);
        if (SUCCEEDED(hr))
        {
            memcpy(buf, cpuData.data(), sizeof(T) * cpuData.size());
            this->GetResourcePtr()->Unmap(0, nullptr);
        }
    }
};

// deprecated
class ReadBackBuffer
{
public:
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

    void Release()
    {
        gpuData.Release();
    }
};

class ConstantBuffer
{
    std::mutex lock;
    PerFrame<std::vector<Resource>> pages;
    uint count;
    static constexpr uint pageSize = 128;
    static constexpr uint pageStride = 512;

    struct range
    {
        uint start;
        uint end;
    };
    std::vector<range> ranges;
    void DebugRanges(uint start, uint end)
    {
#if _DEBUG
        for (uint i = 0; i < ranges.size(); i++)
        {
            if (start >= ranges[i].start && start <= ranges[i].end)
            {
                IOs::instance->Log("range overlap start {} end {}, range start {} end {}", start, end, ranges[i].start, ranges[i].end);
            }
        }
#endif
    }

public:
    static ConstantBuffer* instance;

    void On()
    {
        instance = this;
        count = 0;
    }

    void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            auto& res = pages.Get(i);
            for (uint j = 0; j < res.size(); j++)
            {
                res[j].Release();
            }
        }
    }

    void Reset()
    {
        count = 0;
        ranges.clear();
    }

    D3D12_GPU_VIRTUAL_ADDRESS PushConstantBuffer(void* data)
    {
        lock.lock();
        uint page = count / pageSize;
        uint index = count % pageSize;
        count++;
        if (pages->size() <= page)
        {
            auto& newPage = pages->emplace_back();
            newPage.CreateBuffer((pageSize + 1) * pageStride, pageStride, true, "constant buffer");
        }
        auto& pageBuff = pages.Get()[page];

        char* buf;
        D3D12_RANGE rangeRead = { 0 , 0 }; // dont read
        D3D12_RANGE rangeWrite = { index * pageStride, index * pageStride + pageStride };
        auto hr = pageBuff.GetResource()->Map(0, &rangeRead, (void**)&buf);
        if (SUCCEEDED(hr))
        {
            DebugRanges((uint)rangeWrite.Begin, (uint)rangeWrite.End);
            buf += index * pageStride;
            memcpy(buf, data, pageStride);
            pageBuff.GetResource()->Unmap(0, &rangeWrite);
        }
        lock.unlock();
        return pageBuff.GetResource()->GetGPUVirtualAddress() + (index * pageStride);
    }
};
ConstantBuffer* ConstantBuffer::instance;

// https://github.com/TheRealMJP/DeferredTexturing/blob/experimental/SampleFramework12/v1.01/Graphics/Profiler.cpp
// use https://github.com/Raikiri/LegitProfiler for display ?
struct Profiler
{
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

    ID3D12QueryHeap* queryHeap = NULL;

    struct PerQueueData
    {
        static constexpr UINT64 queueCount = 3;
        static constexpr UINT64 maxProfiles = 128;
        Resource buffer;
        ReadBackBuffer readbackBuffer;
        std::vector<ProfileData> entries;
    };
    PerQueueData queueProfile[3];

    struct FrameData
    {
        uint instancesCount;
        uint meshletsCount;
        uint verticesCount;
    };
    FrameData frameData;

    void On()
    {
        ZoneScoped;
        D3D12_QUERY_HEAP_DESC queryheapDesc = { };
        queryheapDesc.Count = PerQueueData::maxProfiles;
        queryheapDesc.NodeMask = 0;
        queryheapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        GPU::instance->device->CreateQueryHeap(&queryheapDesc, IID_PPV_ARGS(&queryHeap));

        for (uint i = 0; i < PerQueueData::queueCount; i++)
        {
            auto& prof = queueProfile[i];
            prof.entries.reserve(PerQueueData::maxProfiles);
            prof.buffer.CreateBuffer<UINT64>((UINT)(PerQueueData::maxProfiles), "Profile");
        }

        instance = this;
    }

    UINT64 StartProfile(CommandBuffer& cb, const LPCSTR name)
    {
        ZoneScoped;

        uint queueIndex = 0;
        if (cb.queue == GPU::instance->graphicQueue)
            queueIndex = 0;
        if (cb.queue == GPU::instance->computeQueue)
            queueIndex = 1;
        if (cb.queue == GPU::instance->copyQueue)
            queueIndex = 2;

        auto& prof = queueProfile[queueIndex];

        UINT64 profileIdx = UINT64(-1);
        for (UINT64 i = 0; i < prof.entries.size(); ++i)
        {
            if (prof.entries[i].name == name)
            {
                profileIdx = i;
                break;
            }
        }

        if (profileIdx == UINT64(-1))
        {
            profileIdx = prof.entries.size();
            ProfileData data = {};
            data.name = name;
            prof.entries.push_back(data);
        }

        seedAssert(profileIdx != UINT64(-1));
        seedAssert(profileIdx < PerQueueData::maxProfiles);

        ProfileData& profileData = prof.entries[profileIdx];
        profileData.Active = true;
        profileData.QueryStarted = true;

        // Insert the start timestamp
        cb.cmd->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (uint)profileIdx * 2);
        cb.profileIdx = (uint)profileIdx;

        return profileIdx;
    }

    void EndProfile(CommandBuffer& cb)
    {
        ZoneScoped;

        if (cb.profileIdx != UINT(-1))
        {
            uint queueIndex = 0;
            if (cb.queue == GPU::instance->graphicQueue)
                queueIndex = 0;
            if (cb.queue == GPU::instance->computeQueue)
                queueIndex = 1;
            if (cb.queue == GPU::instance->copyQueue)
                queueIndex = 2;

            auto& prof = queueProfile[queueIndex];

            ProfileData& profileData = prof.entries[cb.profileIdx];

            // Insert the end timestamp
            cb.cmd->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, cb.profileIdx * 2 + 1);

            // Resolve the data
            const UINT64 dstOffset = (cb.profileIdx * 2) * sizeof(UINT64);
            cb.cmd->ResolveQueryData(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, cb.profileIdx * 2, 2, prof.buffer.GetResource(), dstOffset);
        }
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

        cb.queue->GetTimestampFrequency(&gpuFrequency);

        for (uint i = 0; i < ARRAYSIZE(queueProfile); i++)
        {
            auto& prof = queueProfile[i];
            prof.readbackBuffer.ReadBack(prof.buffer, cb);

            UINT64* queryData = NULL;
            ID3D12Resource* res = prof.readbackBuffer.gpuData.GetResource();
            res->Map(0, nullptr, (void**)&queryData);

            // Iterate over all of the profiles
            for (UINT64 profileIdx = 0; profileIdx < prof.entries.size(); ++profileIdx)
                UpdateProfile(prof.entries[profileIdx], profileIdx, gpuFrequency, queryData);

            res->Unmap(0, nullptr);
        }

    }

    void Off()
    {
        ZoneScoped;
        for (uint i = 0; i < ARRAYSIZE(queueProfile); i++)
        {
            auto& prof = queueProfile[i];
            prof.buffer.Release();
            prof.readbackBuffer.Release();
        }
        queryHeap->Release();
    }
};
Profiler* Profiler::instance;

struct Vertex // et pas HLSL::Vertex car a cause de hlsl++ il sera pas aligne pareil mais il doivent rester les memes !
{
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz;
    float bx, by, bz;
    float u, v, u1, v1;
};

struct Meshlet : HLSL::Meshlet
{
};

struct Mesh : HLSL::Mesh
{
    Resource BLAS;
};

struct MeshData
{
    std::vector<Meshlet> meshlets;
    std::vector<unsigned int> meshlet_vertices;
    std::vector<unsigned char> meshlet_triangles;
    std::vector<unsigned int> indices;
    std::vector<Vertex> vertices;
    float4 boundingSphere;
};

/*
struct ResourcePool
{
    uint poolSize;
    uint nextOffset;
    uint stride;
    std::vector<Resource> resource;

};
*/

struct MeshStorage
{
    static MeshStorage* instance;

    uint nextMeshOffset = 0;
    Resource meshes;
    uint nextMeshletOffset = 0;
    Resource meshlets;
    uint nextMeshletVertexOffset = 0;
    Resource meshletVertices;
    uint nextMeshletTriangleOffset = 0;
    Resource meshletTriangles;
    uint nextVertexOffset = 0;
    Resource vertices;
    uint nextIndexOffset = 0;
    Resource indices;

    // just to keep the BLAS so it can be released
    // could we just store BLASes in big bulk resources like above ?
    std::vector<Mesh> allMeshes;
    Resource scratchBLAS;
    uint maxScratchSizeInBytes = 150000000;

    std::recursive_mutex lock;

    uint meshesMaxCount = 512;
    uint meshletMaxCount = 200000;
    uint meshletVertexMaxCount = meshletMaxCount * HLSL::max_vertices;
    uint meshletTrianglesMaxCount = meshletMaxCount * HLSL::max_triangles;
    uint vertexMaxCount = meshletVertexMaxCount;
    uint indexMaxCount = meshletTrianglesMaxCount;

    StructuredUploadBuffer<float4x4> identityMatrix;

    void On()
    {
        instance = this;
        meshes.CreateBuffer<HLSL::Mesh>(meshesMaxCount, "meshes");
        meshlets.CreateBuffer<HLSL::Meshlet>(meshletMaxCount, "meshlets");
        meshletVertices.CreateBuffer<unsigned int>(meshletVertexMaxCount, "meshlet vertices");
        meshletTriangles.CreateBuffer<unsigned char>(meshletTrianglesMaxCount, "meshlet triangles");
        vertices.CreateBuffer<Vertex>(vertexMaxCount, "vertices");
        indices.CreateBuffer<unsigned int>(indexMaxCount, "indices");

        scratchBLAS.CreateBuffer(maxScratchSizeInBytes, 4, false, "RayTracingScratch", D3D12_RESOURCE_STATE_COMMON);

        float4x4 iden = float4x4::identity();
        identityMatrix.Add(iden);
        identityMatrix.Upload();
    }

    void Off()
    {
        meshes.Release();
        meshlets.Release();
        meshletVertices.Release();
        meshletTriangles.Release();
        vertices.Release();
        indices.Release();
        scratchBLAS.Release();
        identityMatrix.Release();
        instance = nullptr;
    }

    Mesh Load(MeshData& meshData, CommandBuffer& commandBuffer)
    {
        ZoneScoped;

        lock.lock();
        uint _nextMeshOffset = nextMeshOffset;
        uint _nextMeshletOffset = nextMeshletOffset;
        uint _nextMeshletVertexOffset = nextMeshletVertexOffset;
        uint _nextMeshletTriangleOffset = nextMeshletTriangleOffset;
        uint _nextVertexOffset = nextVertexOffset;
        uint _nextIndexOffset = nextIndexOffset;

        nextMeshOffset += 1;
        nextMeshletOffset += (uint)meshData.meshlets.size();
        nextMeshletVertexOffset += (uint)meshData.meshlet_vertices.size();
        nextMeshletTriangleOffset += (uint)meshData.meshlet_triangles.size();
        nextVertexOffset += (uint)meshData.vertices.size();
        nextIndexOffset += (uint)meshData.indices.size();


        seedAssert(nextVertexOffset < meshletVertexMaxCount);
        seedAssert(nextIndexOffset < meshletTrianglesMaxCount);

        lock.unlock();

        Mesh newMesh;
        newMesh.boundingSphere = meshData.boundingSphere;
        newMesh.meshletCount = (uint)meshData.meshlets.size();
        newMesh.meshletOffset = _nextMeshletOffset;
        newMesh.indexCount = (uint)meshData.indices.size();
        newMesh.indexOffset = _nextIndexOffset;
        newMesh.vertexCount = (uint)meshData.vertices.size();
        newMesh.vertexOffset = _nextVertexOffset;
        newMesh.storageIndex = _nextMeshOffset;
        for (uint i = 0; i < meshData.meshlets.size(); i++)
        {
            meshData.meshlets[i].triangleOffset += _nextMeshletTriangleOffset;
            meshData.meshlets[i].vertexOffset += _nextMeshletVertexOffset;

        }
        for (uint j = 0; j < meshData.meshlet_vertices.size(); j++)
        {
            meshData.meshlet_vertices[j] += _nextVertexOffset;
        }
        for (uint j = 0; j < meshData.indices.size(); j++)
        {
            meshData.indices[j] += _nextVertexOffset;
        }

        meshes.UploadElements(&newMesh, 1, _nextMeshOffset, commandBuffer);
        meshlets.UploadElements(meshData.meshlets.data(), (uint)meshData.meshlets.size(), _nextMeshletOffset, commandBuffer);
        meshletVertices.UploadElements(meshData.meshlet_vertices.data(), (uint)meshData.meshlet_vertices.size(), _nextMeshletVertexOffset, commandBuffer);
        meshletTriangles.UploadElements(meshData.meshlet_triangles.data(), (uint)meshData.meshlet_triangles.size(), _nextMeshletTriangleOffset, commandBuffer);
        vertices.UploadElements(meshData.vertices.data(), (uint)meshData.vertices.size(), _nextVertexOffset, commandBuffer);
        indices.UploadElements(meshData.indices.data(), (uint)meshData.indices.size(), _nextIndexOffset, commandBuffer);

        meshes.Transition(commandBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        meshlets.Transition(commandBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        meshletVertices.Transition(commandBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        meshletTriangles.Transition(commandBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

        vertices.Transition(commandBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        indices.Transition(commandBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        LoadBLAS(newMesh, commandBuffer);

        vertices.Transition(commandBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        indices.Transition(commandBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);

        return newMesh;
    }

    void LoadBLAS(Mesh& mesh, CommandBuffer& commandBuffer)
    {
        lock.lock();
        bool isOpaque = true;
        D3D12_RAYTRACING_GEOMETRY_DESC descriptor = {};
        descriptor.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        descriptor.Triangles.VertexBuffer.StartAddress = vertices.GetResource()->GetGPUVirtualAddress();
        descriptor.Triangles.VertexBuffer.StrideInBytes = vertices.stride;
        descriptor.Triangles.VertexCount = mesh.vertexCount;
        descriptor.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        descriptor.Triangles.IndexBuffer = indices.GetResource()->GetGPUVirtualAddress() + mesh.indexOffset * indices.stride;
        descriptor.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        descriptor.Triangles.IndexCount = mesh.indexCount;
        descriptor.Triangles.Transform3x4 = identityMatrix.GetResourcePtr()->GetGPUVirtualAddress();
        descriptor.Flags = isOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildDesc;
        prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        prebuildDesc.NumDescs = 1;
        prebuildDesc.pGeometryDescs = &descriptor;
        prebuildDesc.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        GPU::instance->device->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);

        if (info.ScratchDataSizeInBytes <= 0 || info.ResultDataMaxSizeInBytes <= 0)// || info.UpdateScratchDataSizeInBytes <= 0)
        {
            IOs::Log("Raytracing BLAS Creation Error");
        }
        seedAssert(info.ScratchDataSizeInBytes < maxScratchSizeInBytes);

        // Buffer sizes need to be 256-byte-aligned
        UINT64 scratchSizeInBytes = ROUND_UP(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        UINT64 resultSizeInBytes = ROUND_UP(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        if (scratchSizeInBytes > maxScratchSizeInBytes)
        {
            maxScratchSizeInBytes = (uint)scratchSizeInBytes;
            //release deferred scratchBLAS
            //reallocate scratchBLAS
        }

        mesh.BLAS.CreateAccelerationStructure((uint)resultSizeInBytes, "BLAS");

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc;
        buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        buildDesc.Inputs.NumDescs = 1;
        buildDesc.Inputs.pGeometryDescs = &descriptor;
        buildDesc.DestAccelerationStructureData = mesh.BLAS.GetResource()->GetGPUVirtualAddress();
        buildDesc.ScratchAccelerationStructureData = scratchBLAS.GetResource()->GetGPUVirtualAddress();
        buildDesc.SourceAccelerationStructureData = 0;
        buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        commandBuffer.cmd->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        D3D12_RESOURCE_BARRIER uavBarrier;
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = mesh.BLAS.GetResource();
        uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        commandBuffer.cmd->ResourceBarrier(1, &uavBarrier);

        lock.unlock();
    }

    // TODO : a real release in meshStorage
};
MeshStorage* MeshStorage::instance;
