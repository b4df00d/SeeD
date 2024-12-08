#pragma once

#include <map>
#include "Shaders/structs.hlsl"


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

static constexpr uint invalidMapIndex = UINT_MAX;
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
    uint maxLoading = 10;

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
    void SetLoaded(uint index, bool value)
    {
        loaded[index] = value;
    }
    cpuType& GetData(uint index)
    {
        return data[index];
    }
    gpuType& GetGPUData(uint index)
    {
        return gpuData[index];
    }
    void Upload()
    {
        gpuData.Upload();
    }
};

// life time : program
struct GlobalResources
{
    Map<assetID, Shader, HLSL::Shader> shaders;
    Map<assetID, Mesh, HLSL::Mesh> meshes;
    Map<assetID, Resource, HLSL::Texture> textures;

    void Release()
    {
        shaders.Release();
        meshes.Release();
        textures.Release();
    }
};

// life time : frame
struct ViewWorld
{
    StructuredUploadBuffer<HLSL::Camera> cameras;
    StructuredUploadBuffer<HLSL::Light> lights;
    Map<World::Entity, Material, HLSL::Material> materials;
    StructuredUploadBuffer<HLSL::Instance> instances;
    StructuredUploadBuffer<HLSL::Instance> instancesGPU; // only for instances created on GPU
    TLAS tlas;

    void Release()
    {
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
    StructuredUploadBuffer<HLSL::Camera> camera;
    StructuredUploadBuffer<HLSL::Instance> instancesInView;
    StructuredUploadBuffer<HLSL::Light> lights;
};

class View
{
public:
    PerFrame<ViewWorld> rendererWorld;
    CullingContext cullingContext;
    uint2 resolution;

    virtual void On(IOs::WindowInformation& window, GlobalResources& globalResources) = 0;
    virtual void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            rendererWorld.Get(i)->Release();
        }
    }
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
        uint index;
        globalResources.shaders.Add(meshShader.Get().id, index);
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
class MainView : public View
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
        View::Off();
    }

    tf::Task Schedule(World& world, GlobalResources& globalResources, tf::Subflow& subflow) override
    {
        ZoneScoped;

        tf::Task updateInstances = UpdateInstances(world, globalResources, subflow);
        tf::Task updateMaterials = UpdateMaterials(world, globalResources, subflow);
        tf::Task updateLights = UpdateLights(world, globalResources, subflow);
        tf::Task updateCameras = UpdateCameras(world, globalResources, subflow);

        tf::Task updloadInstancesBuffers = UploadInstancesBuffers(world, globalResources, subflow);
        tf::Task updloadMeshesBuffers = UploadMeshesBuffers(world, globalResources, subflow);
        tf::Task uploadShadersBuffers = UploadShadersBuffers(world, globalResources, subflow);
        tf::Task uploadTexturesBuffers = UploadTexturesBuffers(world, globalResources, subflow);

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

        updateInstances.precede(skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);
        presentTask.succeed(skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);

        updateInstances.precede(updloadInstancesBuffers, updloadMeshesBuffers, uploadShadersBuffers, uploadTexturesBuffers);
        presentTask.succeed(updloadInstancesBuffers, updloadMeshesBuffers, uploadShadersBuffers, uploadTexturesBuffers);

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

    tf::Task UpdateInstances(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;
        rendererWorld->instances.Clear();


#define stepSize 128
        ViewWorld* frameWorld = rendererWorld.Get();
        
        uint instanceQueryIndex = world.Query(Components::Instance::mask | Components::Transform::mask, 0);
        uint entityCount = (uint)world.frameQueries[instanceQueryIndex].size();
        frameWorld->instances.Reserve(entityCount);

        uint materialsCount = world.CountQuery(Components::Material::mask, 0);
        frameWorld->materials.Reserve(materialsCount);

        uint meshesCount = world.CountQuery(Components::Mesh::mask, 0);
        globalResources.meshes.Reserve(meshesCount);

        uint texturesCount = world.CountQuery(Components::Texture::mask, 0);
        globalResources.textures.Reserve(texturesCount);

        uint shadersCount = world.CountQuery(Components::Shader::mask, 0);
        globalResources.shaders.Reserve(shadersCount);

        
        tf::Task task = subflow.for_each_index(0, entityCount, stepSize, 
            [&world, &globalResources, frameWorld, instanceQueryIndex](int i)
            {
                ZoneScopedN("UpdateInstance");

                std::array<HLSL::Instance, stepSize> localInstances;
                uint instanceCount = 0;
                for (uint subQuery = 0; subQuery < stepSize; subQuery++)
                {
                    auto& queryResult = world.frameQueries[instanceQueryIndex];
                    if (i + subQuery > queryResult.size() - 1) return;

                    Components::Instance& instanceCmp = queryResult[i + subQuery].Get<Components::Instance>();
                    Components::Mesh& meshCmp = instanceCmp.mesh.Get();
                    Components::Material& materialCmp = instanceCmp.material.Get();
                    Components::Shader& shaderCmp = materialCmp.shader.Get();

                    uint meshIndex;
                    bool meshAdded = globalResources.meshes.Add(meshCmp.id, meshIndex);
                    if (!globalResources.meshes.GetLoaded(meshIndex)) continue;

                    uint shaderIndex;
                    bool shaderAdded = globalResources.shaders.Add(shaderCmp.id, shaderIndex);
                    if (!globalResources.shaders.GetLoaded(shaderIndex)) continue;

                    uint materialIndex;
                    bool materialAdded = frameWorld->materials.Add(World::Entity(instanceCmp.material.index), materialIndex);
                    if (!frameWorld->materials.GetLoaded(materialIndex)) continue;

                    // everything should be loaded to be able to draw the instance

                    Components::Transform& transformCmp = queryResult[i + subQuery].Get<Components::Transform>();

                    HLSL::Instance& instance = localInstances[instanceCount];
                    instance.meshIndex = meshIndex;
                    instance.materialIndex = materialIndex;
                    instance.worldMatrix = transformCmp.matrix;

                    // if in range (depending on distance and BC size)
                        // Add to TLAS
                    // count instances with shader

                    instanceCount++;
                }

                frameWorld->instances.AddRange(localInstances.data(), instanceCount);

            }
        );

        return task;
    }

    tf::Task UpdateMaterials(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;
        // parallel for materials in frameworld
            // add textures in globalResources
            // load textures from disk
            // upload textures
            // upload materials

        uint queryIndex = world.Query(Components::Material::mask, 0);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        uint entityStep = 1024;
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, 
            [&world, &globalResources, frameWorld, queryIndex](int i)
            {
                ZoneScopedN("UpdateMaterials");
                for (uint subQuery = 0; subQuery < stepSize; subQuery++)
                {
                    auto& queryResult = world.frameQueries[queryIndex];
                    if (i + subQuery > queryResult.size() - 1) return;

                    uint materialIndex;
                    if (frameWorld->materials.Contains(World::Entity(queryResult[i + subQuery].Get<Components::Entity>().index), materialIndex))
                    {
                        Components::Material& materialCmp = queryResult[i + subQuery].Get<Components::Material>();
                        Material& material = frameWorld->materials.GetData(materialIndex);

                        uint shaderIndex;
                        bool shaderAdded = globalResources.shaders.Contains(materialCmp.shader.Get().id, shaderIndex);

                        material.shaderIndex = shaderIndex;
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
                                uint textureIndex;
                                bool textureAdded = globalResources.textures.Add(textureCmp.id, textureIndex);
                                if (!globalResources.textures.GetLoaded(textureIndex)) materialReady = false;
                                material.texturesSRV[texIndex] = globalResources.textures.GetData(textureIndex).srv;
                            }
                        }

                        frameWorld->materials.SetLoaded(materialIndex, materialReady);
                    }
                }
            }
        );
        return task;
    }

    tf::Task UpdateLights(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;
        // upload lights
        uint queryIndex = world.Query(Components::Light::mask, 0);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        uint entityStep = 1;
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, frameWorld, queryIndex](int i)
            {
                ZoneScopedN("UpdateLights");

            }
        );
        return task;
    }

    tf::Task UpdateCameras(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;
        // upload camera

        uint queryIndex = world.Query(Components::Camera::mask, 0);

        uint entityCount = (uint)world.frameQueries[queryIndex].size();
        uint entityStep = 1;
        ViewWorld* frameWorld = rendererWorld.Get();

        tf::Task task = subflow.for_each_index(0, entityCount, entityStep, [&world, frameWorld, queryIndex](int i)
            {
                ZoneScopedN("UpdateCameras");

            }
        );
        return task;
    }

    tf::Task UploadInstancesBuffers(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            [this]()
            {
                this->rendererWorld->instances.Upload();
            }
        ).name("upload instances buffer");

        return task;
    }

    tf::Task UploadMeshesBuffers(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            [&globalResources]() 
            {
                for (uint i = 0; i < globalResources.meshes.count; i++)
                {
                    auto& cpu = globalResources.meshes.GetData(i);
                    auto& gpu = globalResources.meshes.GetGPUData(i);
                    gpu.meshOffset = cpu.meshOffset;
                    gpu.meshletCount = cpu.meshletCount;
                }
                globalResources.meshes.Upload();
            }
        ).name("upload meshes buffer");

        return task;
    }

    tf::Task UploadShadersBuffers(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            [&globalResources]()
            {
                for (uint i = 0; i < globalResources.shaders.count; i++)
                {
                    auto& cpu = globalResources.shaders.GetData(i);
                    auto& gpu = globalResources.shaders.GetGPUData(i);
                    gpu.id = i;//cpu.something;
                }
                globalResources.shaders.Upload();
            }
        ).name("upload shaders buffer");

        return task;
    }

    tf::Task UploadTexturesBuffers(World& world, GlobalResources& globalResources, tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            [&globalResources]()
            {
                for (uint i = 0; i < globalResources.textures.count; i++)
                {
                    auto& cpu = globalResources.textures.GetData(i);
                    auto& gpu = globalResources.textures.GetGPUData(i);
                    gpu.index = cpu.srv.offset;
                }
                globalResources.textures.Upload();
            }
        ).name("upload textures buffer");

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
        globalResources.Release();
    }

    void Schedule(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        auto mainViewEndTask = mainView.Schedule(world, globalResources, subflow);
        auto editorViewEndTask = editorView.Schedule(world, globalResources, subflow);

        SUBTASKRENDERER(ExecuteFrame);
        SUBTASKRENDERER(WaitFrame);
        SUBTASKRENDERER(PresentFrame);

        ExecuteFrame.succeed(mainViewEndTask, editorViewEndTask);
        ExecuteFrame.precede(WaitFrame);
        WaitFrame.precede(PresentFrame);
    }

    void ScheduleLoading(tf::Subflow& subflow)
    {
        ZoneScoped;
        uint loadingLeft = globalResources.shaders.maxLoading;
        for (uint i = 0; i < globalResources.shaders.count; i++)
        {
            if (!globalResources.shaders.GetLoaded(i))
            {
                // load shader
                //SUBTASKRENDERER(LoadShaders);
                tf::Task pass = subflow.emplace([this, i]() {this->LoadShaders(i); }).name("LoadShaders");
                loadingLeft--;
            }
            if (loadingLeft == 0)
                break;
        }
        loadingLeft = globalResources.meshes.maxLoading;
        for (uint i = 0; i < globalResources.meshes.count; i++)
        {
            if (!globalResources.meshes.GetLoaded(i))
            {
                // load mesh
                //SUBTASKRENDERER(LoadMeshes);
                tf::Task pass = subflow.emplace([this, i]() {this->LoadMeshes(i); }).name("LoadMeshes");
                loadingLeft--;
            }
            if (loadingLeft == 0)
                break;
        }
        loadingLeft = globalResources.textures.maxLoading;
        for (uint i = 0; i < globalResources.textures.count; i++)
        {
            if (!globalResources.textures.GetLoaded(i))
            {
                // load texture
                //SUBTASKRENDERER(LoadTextures);
                tf::Task pass = subflow.emplace([this, i]() {this->LoadTextures(i); }).name("LoadTextures");
                loadingLeft--;
            }
            if (loadingLeft == 0)
                break;
        }
    }

    // do that in backgroud task ?
    void LoadShaders(uint i)
    {
        ZoneScoped;
        IOs::Log("shader");
        globalResources.shaders.GetData(i);
        globalResources.shaders.SetLoaded(i, true);
    }
    void LoadMeshes(uint i)
    {
        ZoneScoped;
        IOs::Log("mesh");
        globalResources.meshes.GetData(i);
        globalResources.meshes.SetLoaded(i, true);
    }
    void LoadTextures(uint i)
    {
        ZoneScoped;
        IOs::Log("texture");
        globalResources.textures.GetData(i);
        globalResources.textures.SetLoaded(i, true);
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
