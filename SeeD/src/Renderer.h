#pragma once

#include <map>
#include "Shaders/structs.hlsl"

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
    StructuredUploadBuffer<HLSL::Instance> instancesGPU; // only for instances created on GPU
    TLAS tlas;

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
    StructuredBuffer<HLSL::MeshletDrawCall> meshletsInView;
    StructuredBuffer<uint> meshletsInViewCounter;

    void Release()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            cullingContext.Get(i).Release();
        }
        camera.Release();
        lights.Release();
        meshletsInView.Release();
        meshletsInViewCounter.Release();
    }
};

class View
{
public:
    PerFrame<ViewWorld> viewWorld;
    CullingContext cullingContext;
    uint2 resolution;

    virtual void On(IOs::WindowInformation& window) = 0;
    virtual void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            viewWorld.Get(i).Release();
        }
        cullingContext.Release();
    }
    virtual tf::Task Schedule(World& world, tf::Subflow& subflow) = 0;
    virtual void Execute() = 0;
    void SetupViewParams()
    {
        HLSL::CommonResourcesIndices commonResourcesIndices;

        commonResourcesIndices.meshesHeapIndex = GlobalResources::instance->meshStorage.meshes.srv.offset;
        commonResourcesIndices.meshCount = GlobalResources::instance->meshStorage.nextMeshOffset;
        commonResourcesIndices.meshletsHeapIndex = GlobalResources::instance->meshStorage.meshlets.srv.offset;
        commonResourcesIndices.meshletCount = GlobalResources::instance->meshStorage.nextMeshletOffset;
        commonResourcesIndices.meshletVerticesHeapIndex = GlobalResources::instance->meshStorage.meshletVertices.srv.offset;
        commonResourcesIndices.meshletVertexCount = GlobalResources::instance->meshStorage.nextMeshletVertexOffset;
        commonResourcesIndices.meshletTrianglesHeapIndex = GlobalResources::instance->meshStorage.meshletTriangles.srv.offset;
        commonResourcesIndices.meshletTriangleCount = GlobalResources::instance->meshStorage.nextMeshletTriangleOffset;
        commonResourcesIndices.verticesHeapIndex = GlobalResources::instance->meshStorage.vertices.srv.offset;
        commonResourcesIndices.vertexCount = GlobalResources::instance->meshStorage.nextVertexOffset;
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
        cullingContextParams.culledInstanceIndex = cullingContext.meshletsInView.GetResource().uav.offset;
        cullingContextParams.culledInstanceCounterIndex = cullingContext.meshletsInViewCounter.GetResource().uav.offset;

        cullingContext.cullingContext->Clear();
        cullingContext.cullingContext->Add(cullingContextParams);
        cullingContext.cullingContext->Upload();
    }
};

class Pass
{
public:
    PerFrame<CommandBuffer> commandBuffer;
    Resource renderTargets[8];
    Resource depthBuffer;

    // debug only ?
    String name;

    virtual void On(View* view, bool asyncCompute, String _name)
    {
        ZoneScoped;
        name = _name;
        //name = CharToWString(typeid(this).name()); // name = "class Pass * __ptr64"

        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i).On(asyncCompute, name);
        }
    }

    void Off()
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

    void SetupView(View* view, bool clearRT, bool clearDepth)
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
        commandBuffer->cmd->OMSetRenderTargets(1, &renderTargets[0].rtv.handle, false, &depthBuffer.dsv.handle);

        float4 clearColor(0.4f, 0.1f, 0.2f, 0.0f);
        commandBuffer->cmd->ClearRenderTargetView(renderTargets[0].rtv.handle, clearColor.f32, 1, &rect);

        float clearDepthValue(1.0f);
        UINT8 clearStencilValue(0);
        commandBuffer->cmd->ClearDepthStencilView(depthBuffer.dsv.handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clearDepthValue, clearStencilValue, 1, &rect);
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
    Components::Handle<Components::Shader> cullingShader;
    Components::Handle<Components::Shader> cullingResetShader;
public:
    virtual void On(View* view, bool asyncCompute, String _name) override
    {
        Pass::On(view, asyncCompute, _name);
        ZoneScoped;
        cullingShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\culling.hlsl");
        cullingResetShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\cullingReset.hlsl");
        view->cullingContext.meshletsInView.CreateBuffer(0, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        view->cullingContext.meshletsInViewCounter.CreateBuffer(0, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    }
    void Setup(View* view) override
    {
        ZoneScoped;
    }
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        view->cullingContext.meshletsInView.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
        view->cullingContext.meshletsInViewCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);

        auto& instances = view->viewWorld->instances;

        Shader& reset = *AssetLibrary::instance->Get<Shader>(cullingResetShader.Get().id, true);
        commandBuffer->SetCompute(reset);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, view->viewWorld->commonResourcesIndices.GetGPUVirtualAddress(0));
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, view->cullingContext.cullingContext->GetGPUVirtualAddress(0));
        commandBuffer->cmd->Dispatch(1, 1, 1);

        Shader& culling = *AssetLibrary::instance->Get<Shader>(cullingShader.Get().id, true);
        culling.numthreads = uint3(64, 1, 1);
        commandBuffer->SetCompute(culling);
        commandBuffer->cmd->SetComputeRootConstantBufferView(0, view->viewWorld->commonResourcesIndices.GetGPUVirtualAddress(0));
        commandBuffer->cmd->SetComputeRootConstantBufferView(1, view->cullingContext.cullingContext->GetGPUVirtualAddress(0));
        commandBuffer->cmd->Dispatch(culling.DispatchX(instances.Size()), 1, 1);

        view->cullingContext.meshletsInView.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        view->cullingContext.meshletsInViewCounter.GetResource().Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

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
    virtual void On(View* view, bool asyncCompute, String _name) override
    {
        Pass::On(view, asyncCompute, _name);
        ZoneScoped;
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

        renderTargets[0] = GPU::instance->backBuffer.Get();
        GPU::instance->backBuffer->Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        SetupView(view, true, true);

        Shader& shader = *AssetLibrary::instance->Get<Shader>(meshShader.Get().id, true);
        commandBuffer->SetGraphic(shader);

        commandBuffer->cmd->SetGraphicsRootConstantBufferView(0, view->viewWorld->commonResourcesIndices.GetGPUVirtualAddress());
        commandBuffer->cmd->SetGraphicsRootConstantBufferView(1, view->cullingContext.cullingContext->GetGPUVirtualAddress());

        uint maxDraw = view->cullingContext.meshletsInView.Size();
        commandBuffer->cmd->ExecuteIndirect(shader.commandSignature, maxDraw, view->cullingContext.meshletsInView.GetResourcePtr(), 0, view->cullingContext.meshletsInViewCounter.GetResourcePtr(), 0);

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

    Resource depthBuffer;

    void On(IOs::WindowInformation& window) override
    {
        resolution = window.windowResolution;

        depthBuffer.CreateDepthTarget(resolution, "Depth");

        skinning.On(this, false, "skinning");
        particles.On(this, false, "particles");
        spawning.On(this, false, "spawning");
        culling.On(this, false, "culling");
        zPrepass.On(this, false, "zPrepass");
        gBuffers.On(this, false, "gBuffers");
        lighting.On(this, false, "lighting");
        forward.On(this, false, "forward");
        forward.renderTargets[0] = GPU::instance->backBuffer.Get();
        forward.depthBuffer = depthBuffer;
        postProcess.On(this, false, "postProcess");
        present.On(this, false, "present");

        //AssetLibrary::instance->LoadMandatory();
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

        depthBuffer.Release();

        View::Off();
    }

    tf::Task Schedule(World& world, tf::Subflow& subflow) override
    {
        ZoneScoped;
        SetupViewParams(); // je devrais pas essayer de mettre ca dans le execute ? mais a ce moment comment je connais le GPUVirtualAdress dans les pass ?!
        SetupCullingContextParams(); // je devrais pas essayer de mettre ca dans le execute ? mais a ce moment comment je connais le GPUVirtualAdress dans les pass ?!

        tf::Task updateInstances = UpdateInstances(world, subflow);
        tf::Task updateMaterials = UpdateMaterials(world, subflow);
        tf::Task updateLights = UpdateLights(world, subflow);
        tf::Task updateCameras = UpdateCameras(world, subflow);

        tf::Task updloadInstancesBuffers = UploadInstancesBuffers(world, subflow);

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

        updateInstances.precede(updateMaterials, updloadInstancesBuffers);
        updloadInstancesBuffers.precede(skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);

        presentTask.succeed(updateInstances, updateMaterials, updloadInstancesBuffers);
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

    tf::Task UpdateInstances(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;
        viewWorld->instances.Clear();


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
                }

                frameWorld.instances.AddRange(localInstances.data(), instanceCount);

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

    tf::Task UploadInstancesBuffers(World& world, tf::Subflow& subflow)
    {

        tf::Task task = subflow.emplace(
            [this]()
            {
                ZoneScoped;
                this->viewWorld->instances.Upload();
                this->cullingContext.meshletsInView.Resize(GlobalResources::instance->meshStorage.nextMeshletVertexOffset);
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

        editor.On(this, false, "editor");
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
    GlobalResources globalResources;
    MainView mainView;
    EditorView editorView;

    void On(IOs::WindowInformation& window)
    {
        instance = this;
        globalResources.On();
        mainView.On(window);
        editorView.On(window);
    }
    
    void Off()
    {
        editorView.Off();
        mainView.Off();
        globalResources.Off();
        instance = nullptr;
    }

    void Schedule(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        auto mainViewEndTask = mainView.Schedule(world, subflow);
        auto editorViewEndTask = editorView.Schedule(world, subflow);

        SUBTASKRENDERER(ExecuteFrame);
        SUBTASKRENDERER(WaitFrame);
        SUBTASKRENDERER(PresentFrame);

        UploadMeshesBuffers(subflow).precede(ExecuteFrame);
        UploadShadersBuffers(subflow).precede(ExecuteFrame);
        UploadTexturesBuffers(subflow).precede(ExecuteFrame);

        ExecuteFrame.succeed(mainViewEndTask, editorViewEndTask);
        ExecuteFrame.precede(WaitFrame);
        WaitFrame.precede(PresentFrame);
    }

    tf::Task UploadMeshesBuffers(tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            []()
            {
                auto& meshesAssetLibrary = AssetLibrary::instance->meshes;
                auto& meshesHLSL = GlobalResources::instance->meshes;
                meshesHLSL.Resize(meshesAssetLibrary.size());

                for (uint i = 0; i < meshesAssetLibrary.size(); i++)
                {
                    auto& cpu = meshesAssetLibrary[i];
                    auto& gpu = meshesHLSL[i];
                    gpu.meshletOffset = cpu.meshletOffset;
                    gpu.meshletCount = cpu.meshletCount;
                }
                meshesHLSL.Upload(); // WRONG !! on peut pas se permettre d upload des nouvelles data (surtout si ca demande un resize) pendant qu on a encore une frame en vole
            }
        ).name("upload meshes buffer");

        return task;
    }

    tf::Task UploadShadersBuffers(tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            []()
            {
                auto& shadersAssetLibrary = AssetLibrary::instance->shaders;
                auto& shadersHLSL = GlobalResources::instance->shaders;
                shadersHLSL.Resize(shadersAssetLibrary.size());

                for (uint i = 0; i < shadersAssetLibrary.size(); i++)
                {
                    auto& cpu = shadersAssetLibrary[i];
                    auto& gpu = shadersHLSL[i];
                    gpu.id = i;//cpu.something;
                }
                shadersHLSL.Upload();
            }
        ).name("upload shaders buffer");

        return task;
    }

    tf::Task UploadTexturesBuffers(tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            []()
            {
                auto& texturesAssetLibrary = AssetLibrary::instance->textures;
                auto& texturesHLSL = GlobalResources::instance->textures;
                texturesHLSL.Resize(texturesAssetLibrary.size());

                for (uint i = 0; i < texturesAssetLibrary.size(); i++)
                {
                    auto& cpu = texturesAssetLibrary[i];
                    auto& gpu = texturesHLSL[i];
                    gpu.index = cpu.srv.offset;
                }
                texturesHLSL.Upload();
            }
        ).name("upload textures buffer");

        return task;
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
