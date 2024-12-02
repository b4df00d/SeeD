#pragma once


struct CullingContext
{
    StructuredUploadBuffer<Camera> camera;
    StructuredUploadBuffer<Instance> instancesInView;
    StructuredUploadBuffer<Light> lights;
    TLAS tlas;
};

struct ViewWorld
{
    StructuredUploadBuffer<Camera> cameras;
    StructuredUploadBuffer<Instance> instances;
    StructuredUploadBuffer<Instance> instancesGPU; // only for instances created on GPU
    StructuredUploadBuffer<Material> materials;
    StructuredUploadBuffer<Light> lights;
};

static uint fixedArraySize = 65536;
template<typename T>
class FixedArray
{
public:
    T* data = 0;

    FixedArray()
    {
        data = new T[fixedArraySize];
    }

    ~FixedArray()
    {
        // destroy if this was a non POD ?
        // or let the user destroy the entries before shutdown ?
        /*
        for (uint i = 0; i < count; i++)
        {
            delete data[i];
        }
        */
        delete[] data;
    }

    inline T& operator[] (uint i)
    {
        return data[i];
    }
};

template<typename keyType, typename cpuType>
class Map
{
    std::vector<FixedArray<keyType>*> keys;
    std::vector<FixedArray<cpuType>*> data;
    std::vector<FixedArray<bool>*> loaded;
public:
    std::atomic_uint32_t count = 0;
    std::atomic_uint32_t pageCount = 0;
    std::recursive_mutex lock;

    Map()
    {
        keys.resize(256);
        data.resize(256);
        loaded.resize(256);
        for (uint i = 0; i < 256; i++)
        {
            keys[i] = 0;
            data[i] = 0;
            loaded[i] = 0;
        }
        keys[0] = new FixedArray<keyType>();
        data[0] = new FixedArray<cpuType>();
        loaded[0] = new FixedArray<bool>();
    }

    int Contains(keyType key)
    {
        //std::lock_guard<std::recursive_mutex> lg(lock); // PLZ !!!! not that !
#if 1
        for (uint i = 0; i < count; i++)
        {
            uint pageIndex = i / fixedArraySize;
            uint index = i % fixedArraySize;
            auto& page = *keys[pageIndex]; //not thread safe ? if someone add a page when we are here ?
            if (page[index] == key)
                return i;
        }
#else
        for (uint i = 0; i < pageCount; i++)
        {
            auto& page = *keys[i]; //not thread safe ? if someone add a page when we are here ?
            for (uint j = 0; j < page.count; j++)
            {
                if (page[j] == key)
                    return i * fixedArraySize + j;
            }
        }
#endif
        return -1;
    }

    uint Add(keyType key)
    {
        int index = Contains(key);
        if (index == -1)
        {
            index = count++;
            uint pageIndex = index / fixedArraySize;
            uint localIndex = index % fixedArraySize;
            (*keys[pageIndex])[localIndex] = key;
            (*data[pageIndex])[localIndex] = cpuType();
            (*loaded[pageIndex])[localIndex] = false;

            pageCount.store(pageIndex > pageCount.load() ? pageIndex : pageCount.load());
            
            //not very safe ... may leak some fixed arrays
            if (keys[pageCount+1] == nullptr)
            {
                keys[pageCount+1] = new FixedArray<keyType>();
                data[pageCount+1] = new FixedArray<cpuType>();
                loaded[pageCount+1] = new FixedArray<bool>();
            }
        }
        return index;
    }

    bool GetLoaded(uint index)
    {
        uint pageIndex = index / fixedArraySize;
        uint localIndex = index % fixedArraySize;
        auto& page = *loaded[pageIndex];
        return page[localIndex];
    }
    cpuType& GetData(uint index)
    {
        uint pageIndex = index / fixedArraySize;
        uint localIndex = index % fixedArraySize;
        auto& page = *data[pageIndex];
        return page[localIndex];
    }

    /*
    void Remove(keyType key)
    {
        int index = Contains(key);
        uint page = count / fixedArraySize;
        index = count % fixedArraySize;

        keys[[index] = keys.back();
        keys.pop_back();
        data[index] = data.back();
        data.pop_back();
        loaded[index] = loaded.back();
        loaded.pop_back();
    }
    */
};

struct GlobalResources
{
    Map<assetID, Shader> shaders;
    Map<assetID, Mesh> meshes;
    StructuredUploadBuffer<Mesh> meshesGPU;
    Map<assetID, Resource> textures;
};

class View
{
public:
    PerFrame<ViewWorld> rendererWorld;
    CullingContext cullingContext;
    uint2 resolution;

    virtual void On(IOs::WindowInformation& window, GlobalResources& globalResources) = 0;
    virtual void Off() = 0;
    virtual tf::Task Schedule(World& world, GlobalResources& globalResources, tf::Subflow& subflow) = 0;
    virtual void Execute() = 0;
};

class Pass
{
public:
    PerFrame<CommandBuffer> commandBuffer;
    String name;

    virtual void On(GlobalResources& globalResources, bool asyncCompute, String _name)
    {
        ZoneScoped;
        name = _name;
        //name = CharToWString(typeid(this).name()); // name = "class Pass * __ptr64"

        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            D3D12_COMMAND_LIST_TYPE type = asyncCompute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;
            commandBuffer.Get(i)->queue = asyncCompute ? GPU::instance->computeQueue : GPU::instance->graphicQueue;
            HRESULT hr = GPU::instance->device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandBuffer.Get(i)->cmdAlloc));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = commandBuffer.Get(i)->cmdAlloc->SetName(name.ToConstWChar());
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = GPU::instance->device->CreateCommandList(0, type, commandBuffer.Get(i)->cmdAlloc, NULL, IID_PPV_ARGS(&commandBuffer.Get(i)->cmd));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = commandBuffer.Get(i)->cmd->SetName(name.ToConstWChar());
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = commandBuffer.Get(i)->cmd->Close();
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = GPU::instance->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&commandBuffer.Get(i)->passEnd.fence));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            commandBuffer.Get(i)->passEnd.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (commandBuffer.Get(i)->passEnd.fenceEvent == nullptr)
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
        }
    }

    void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i)->cmd->Release();
            commandBuffer.Get(i)->cmdAlloc->Release();
            commandBuffer.Get(i)->passEnd.fence->Release();
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
        Profiler::instance->StartProfile(*commandBuffer.Get(), name.c_str());
    }

    void Close()
    {
        ZoneScoped;

        Profiler::instance->EndProfile(*commandBuffer.Get());
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

class Culling : public Pass
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

class ZPrepass : public Pass
{
public:
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
    Components::Handle<Components::Shader> meshShader;
public:
    virtual void On(GlobalResources& globalResources, bool asyncCompute, String _name) override
    {
        Pass::On(globalResources, asyncCompute, _name);
        ZoneScoped;
        meshShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\mesh.hlsl");
        globalResources.shaders.Add(meshShader.Get().id);
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

        commandBuffer->cmd->OMSetRenderTargets(1, &GPU::instance->backBuffer->rtv.handle, false, nullptr);

        float4 clearColor(0.4f, 0.1f, 0.2f, 0.0f);
        commandBuffer->cmd->ClearRenderTargetView(GPU::instance->backBuffer->rtv.handle, clearColor.f32, 1, &rect);


        //commandBuffer->cmd->DispatchMesh(1, 1, 1);

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
class MainView : View
{
public:
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

    void On(IOs::WindowInformation& window, GlobalResources& globalResources) override
    {
        resolution = window.windowResolution;

        skinning.On(globalResources, false, "skinning");
        particles.On(globalResources, false, "particles");
        spawning.On(globalResources, false, "spawning");
        culling.On(globalResources, false, "culling");
        zPrepass.On(globalResources, false, "zPrepass");
        gBuffers.On(globalResources, false, "gBuffers");
        lighting.On(globalResources, false, "lighting");
        forward.On(globalResources, false, "forward");
        postProcess.On(globalResources, false, "postProcess");
        present.On(globalResources, false, "present");
    }

    void Off() override
    {
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
    }

    tf::Task Schedule(World& world, GlobalResources& globalResources, tf::Subflow& subflow) override
    {
        ZoneScoped;

        tf::Task updateInstances = UpdateInstances(world, globalResources, subflow);
        tf::Task updateMeshes = UpdateMeshes(world, subflow);
        tf::Task updateTextures = UpdateTextures(world, subflow);
        tf::Task updateShaders = UpdateShaders(world, subflow);
        tf::Task updateMaterials = UpdateMaterials(world, subflow);
        tf::Task updateLights = UpdateLights(world, subflow);
        tf::Task updateCameras = UpdateCameras(world, subflow);

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

        updateInstances.precede(updateMeshes, updateTextures, updateShaders);
        updateShaders.precede(skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);
        presentTask.succeed(skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);

        return presentTask;
    }

    void Execute() override
    {
        ZoneScoped;
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

    tf::Task UpdateMeshes(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        // parallel for meshes in GlobalResources
            // load mesh from disk
            // upload mesh
            // update BLAS

        uint queryIndex = world.Query(Components::Mesh::mask, 0);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        uint entityStep = 1;
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, frameWorld, queryIndex](int i)
            {
                ZoneScoped;

            }
        );
        return task;
    }

    tf::Task UpdateTextures(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        // parallel for textures in GlobalResources
            // load mesh from disk
            // upload mesh
            // update BLAS

        uint queryIndex = world.Query(Components::Mesh::mask, 0);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        uint entityStep = 1;
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, frameWorld, queryIndex](int i)
            {
                ZoneScoped;

            }
        );
        return task;
    }

    tf::Task UpdateShaders(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        // parallel for shaders in GlobalResources
            // compile shader

        uint queryIndex = world.Query(Components::Shader::mask, 0);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        uint entityStep = 1;
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, frameWorld, queryIndex](int i)
            {
                ZoneScoped;

            }
        );
        return task;
    }

    tf::Task UpdateInstances(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;
        rendererWorld->instances.Clear();

        uint queryIndex = world.Query(Components::Instance::mask, 0);

#define stepSize 512
        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        //uint entityStep = 128;
        ViewWorld* frameWorld = rendererWorld.Get();
        
        tf::Task task = subflow.for_each_index(0, entityCount, stepSize, [&world, &globalResources, frameWorld, queryIndex](int i)
            {
                ZoneScoped;

                for (uint subQuery = 0; subQuery < stepSize; subQuery++)
                {
                    auto& queryResult = world.frameQueries[queryIndex];
                    if (i + subQuery > queryResult.size() - 1) return;

                    Components::Instance& instanceCmp = queryResult[i + subQuery].Get<Components::Instance>();
                    Components::Mesh& meshCmp = instanceCmp.mesh.Get();
                    Components::Material& materialCmp = instanceCmp.material.Get();
                    Components::Shader& shaderCmp = materialCmp.shader.Get();

                    uint meshIndex = globalResources.meshes.Add(meshCmp.id);
                    if (!globalResources.meshes.GetLoaded(meshIndex)) continue;

                    uint shaderIndex = globalResources.shaders.Add(shaderCmp.id);
                    if (!globalResources.shaders.GetLoaded(shaderIndex)) continue;

                    Material material;
                    for (uint texIndex = 0; texIndex < 16; texIndex++) // keep in sync with number of textures in material
                    {
                        if (materialCmp.textures[texIndex].index != entityInvalid)
                        {
                            Components::Texture& textureCmp = materialCmp.textures[texIndex].Get();
                            uint textureIndex = globalResources.textures.Add(textureCmp.id);
                            if (!globalResources.textures.GetLoaded(textureIndex)) continue;
                            material.texturesSRV[texIndex] = globalResources.textures.GetData(textureIndex).srv;
                        }
                    }

                    // everything should be loaded to be able to draw the instance

                    frameWorld->materials.AddUnique(material);

                    Instance instance;
                    instance.meshIndex = meshIndex;
                    frameWorld->instances.Add(instance);

                    // if in range (depending on distance and BC size)
                        // Add to TLAS
                    // count instances with shader
                }
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
        uint entityStep = 1;
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, frameWorld, queryIndex](int i)
            {
                ZoneScoped;

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
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, frameWorld, queryIndex](int i)
            {
                ZoneScoped;

            }
        );
        return task;
    }

    tf::Task UpdateCameras(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        // upload camera

        uint queryIndex = world.Query(Components::Camera::mask, 0);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        uint entityStep = 1;
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, frameWorld, queryIndex](int i)
            {
                ZoneScoped;

            }
        );
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

    void On(IOs::WindowInformation& window, GlobalResources& globalResources) override
    {
        resolution = window.windowResolution;

        editor.On(globalResources, false, "editor");
    }

    void Off() override
    {
        editor.Off();
    }

    tf::Task Schedule(World& world, GlobalResources& globalResources, tf::Subflow& subflow) override
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
    GlobalResources globalResources;
    MainView mainView;
    EditorView editorView;

    PerFrame<Resource> backBuffer;

    void On(IOs::WindowInformation& window)
    {
        mainView.On(window, globalResources);
        editorView.On(window, globalResources);
    }
    
    void Off()
    {
        mainView.Off();
        editorView.Off();
    }

    void Schedule(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        SUBTASKRENDERER(LoadShaders);
        SUBTASKRENDERER(LoadMeshes);
        SUBTASKRENDERER(LoadTextures);

        auto mainViewEndTask = mainView.Schedule(world, globalResources, subflow);
        auto editorViewEndTask = editorView.Schedule(world, globalResources, subflow);

        SUBTASKRENDERER(ExecuteFrame);
        SUBTASKRENDERER(WaitFrame);
        SUBTASKRENDERER(PresentFrame);

        ExecuteFrame.succeed(mainViewEndTask, editorViewEndTask);
        ExecuteFrame.precede(WaitFrame);
        WaitFrame.precede(PresentFrame);
    }

    // do that in backgroud task ?
    void LoadShaders()
    {
        for (uint i = 0; i < globalResources.shaders.count; i++)
        {
            if (!globalResources.shaders.GetLoaded(i))
            {
                // load shader
            }
        }
    }
    void LoadMeshes()
    {
        for (uint i = 0; i < globalResources.meshes.count; i++)
        {
            if (!globalResources.meshes.GetLoaded(i))
            {
                // load mesh
            }
        }
    }
    void LoadTextures()
    {
        for (uint i = 0; i < globalResources.textures.count; i++)
        {
            if (!globalResources.textures.GetLoaded(i))
            {
                // load texture
            }
        }
    }

    void ExecuteFrame()
    {
        ZoneScoped;
        HRESULT hr;

        mainView.Execute();
        editorView.Execute();
    }

    void WaitFrame()
    {
        ZoneScoped;
        Resource::CleanUploadResources();

        HRESULT hr;
        // if the current fence value is still less than "fenceValue", then we know the GPU has not finished executing
        // the command queue since it has not reached the "commandQueue->Signal(fence, fenceValue)" command
        Fence& previousFrame = editorView.editor.commandBuffer.Get(GPU::instance->frameIndex ? 0 : 1)->passEnd;
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
