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
        assert(present);
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
        assert(present);
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
        assert(present);
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
        assert(present);
        return GetGPUData(index);
    }
    Resource& GetResource()
    {
        return gpuData.GetResource();
    }
    void Upload()
    {
        gpuData.Upload();
    }
    auto begin() { return keys.begin(); }
    auto end() { return keys.end(); }
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
    uint meshletOffset;
    uint meshletCount;
    BLAS blas;
};

struct Material
{
    uint shaderIndex;
    float parameters[15];
    SRV texturesSRV[16];
};

struct MeshStorage
{
    uint nextMeshletOffset;
    Resource meshlets;
    uint nextVertexOffset;
    Resource vertices;
    uint nextIndexOffset;
    Resource indices;

    Resource blasVertices;
    Resource blasIndicies;

    std::recursive_mutex lock;

    void On()
    {
        meshlets.CreateBuffer<HLSL::Meshlet>(1000, "meshlets");
        vertices.CreateBuffer<HLSL::Vertex>(1000, "vertices");
        indices.CreateBuffer<HLSL::Index>(1000, "indices");
    }

    void Off()
    {
        meshlets.Release();
        vertices.Release();
        indices.Release();
    }

    Mesh Add(MeshLoader::Mesh mesh)
    {
        Mesh newMesh;
        lock.lock();
        newMesh.meshletCount = mesh.meshlets.size();
        newMesh.meshletOffset = nextMeshletOffset;
        nextMeshletOffset += newMesh.meshletCount;
        lock.unlock();
        return newMesh;
    }

    Mesh Load(CommandBuffer& commandBuffer)
    {
        ZoneScoped;
        HLSL::Meshlet newMeshlets[10];
        meshlets.Upload(newMeshlets, sizeof(newMeshlets), commandBuffer);

        HLSL::Vertex newVertices[8] =
        {
            {float3(-0.2, 0.2, 0.2), float3(-0.2, 0.2, 0.2), float2(0,0)}, 
            {float3(0.2, 0.2, 0.2), float3(0.2, 0.2, 0.2), float2(0,0)},
            {float3(-0.2, 0.2, 0.8), float3(0,0,0), float2(0,0)},
            {float3(0.2, 0.2, 0.8), float3(0,0,0), float2(0,0)},
            {float3(0.2, -0.2, 0.2), float3(0,0,0), float2(0,0)},
            {float3(0.2, -0.2, 0.2), float3(0,0,0), float2(0,0)},
            {float3(-0.2, -0.2, 0.2), float3(0,0,0), float2(0,0)},
            {float3(-0.2, -0.2, 0.8), float3(0,0,0), float2(0,0)}
        };
        vertices.Upload(newVertices, sizeof(newVertices), commandBuffer);

        HLSL::Index newIndices[12] =
        {
            uint3(0, 1, 2), uint3(3, 1, 2), uint3(3, 4, 5), uint3(6, 4, 5), uint3(6, 7, 8), uint3(9, 7, 8),
            uint3(10, 11, 2), uint3(1, 11, 2), uint3(10, 4, 7), uint3(3, 6, 2), uint3(8, 2, 6), uint3(5, 2, 9)
        };
        indices.Upload(newIndices, sizeof(newIndices), commandBuffer);

        return Mesh();
    }
};

// life time : program
struct GlobalResources
{
    static GlobalResources* instance;
    Map<assetID, Shader, HLSL::Shader> shaders;
    Map<assetID, Mesh, HLSL::Mesh> meshes;
    MeshStorage meshStorage;
    Map<assetID, Resource, HLSL::Texture> textures;

    void On()
    {
        instance = this;
        meshStorage.On();
    }

    void Off()
    {
        meshStorage.Off();
        Release();
        instance = nullptr;
    }

    void Release()
    {
        shaders.Release();
        meshes.Release();
        textures.Release();
    }
};
GlobalResources* GlobalResources::instance = nullptr;

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
    PerFrame<ViewWorld> viewWorld;
    CullingContext cullingContext;
    int2 resolution;

    virtual void On(IOs::WindowInformation& window) = 0;
    virtual void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            viewWorld.Get(i).Release();
        }
    }
    virtual tf::Task Schedule(World& world, tf::Subflow& subflow) = 0;
    virtual void Execute() = 0;
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
            D3D12_COMMAND_LIST_TYPE type = asyncCompute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;
            commandBuffer.Get(i).queue = asyncCompute ? GPU::instance->computeQueue : GPU::instance->graphicQueue;
            HRESULT hr = GPU::instance->device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandBuffer.Get(i).cmdAlloc));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            std::wstring wname = name.ToWString();
            hr = commandBuffer.Get(i).cmdAlloc->SetName(wname.c_str());
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = GPU::instance->device->CreateCommandList(0, type, commandBuffer.Get(i).cmdAlloc, NULL, IID_PPV_ARGS(&commandBuffer.Get(i).cmd));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            std::wstring wname2 = name.ToWString();
            hr = commandBuffer.Get(i).cmd->SetName(wname2.c_str());
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = commandBuffer.Get(i).cmd->Close();
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = GPU::instance->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&commandBuffer.Get(i).passEnd.fence));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            commandBuffer.Get(i).passEnd.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (commandBuffer.Get(i).passEnd.fenceEvent == nullptr)
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
        }
    }

    void Off()
    {
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i).cmd->Release();
            commandBuffer.Get(i).cmdAlloc->Release();
            commandBuffer.Get(i).passEnd.fence->Release();
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

    virtual void Setup(View* view) = 0;
    virtual void Render(View* view) = 0;
};

// globalResources upload (meshes, textures)
class Upload : public Pass
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
        GlobalResources::instance->meshStorage.Load(commandBuffer.Get());
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
    virtual void On(View* view, bool asyncCompute, String _name) override
    {
        Pass::On(view, asyncCompute, _name);
        ZoneScoped;
        meshShader.Get().id = AssetLibrary::instance->Add("src\\Shaders\\mesh.hlsl");
        uint index;
        GlobalResources::instance->shaders.Add(meshShader.Get().id, index);
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

        float clearDepth(1.0f);
        UINT8 clearStencil(0);
        commandBuffer->cmd->ClearDepthStencilView(depthBuffer.dsv.handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clearDepth, clearStencil, 1, &rect);

        bool loaded = GlobalResources::instance->shaders.GetLoaded(meshShader.Get().id);
        if (loaded)
        {
            Shader& shader = GlobalResources::instance->shaders.GetData(meshShader.Get().id);
            commandBuffer->Set(shader);

            HLSL::CommonResourcesIndices commonResourcesIndices;
            commonResourcesIndices.meshletsHeapIndex = GlobalResources::instance->meshStorage.meshlets.srv.offset;
            commonResourcesIndices.verticesHeapIndex = GlobalResources::instance->meshStorage.vertices.srv.offset;
            commonResourcesIndices.indicesHeapIndex = GlobalResources::instance->meshStorage.indices.srv.offset;
            commonResourcesIndices.camerasHeapIndex = view->viewWorld->cameras.gpuData.srv.offset;
            commonResourcesIndices.lightsHeapIndex = view->viewWorld->lights.gpuData.srv.offset;
            commonResourcesIndices.materialsHeapIndex = view->viewWorld->materials.GetResource().srv.offset;
            commonResourcesIndices.instancesHeapIndex = view->viewWorld->instances.gpuData.srv.offset;
            commonResourcesIndices.instancesGPUHeapIndex = view->viewWorld->instancesGPU.gpuData.srv.offset;
            commandBuffer->cmd->SetGraphicsRoot32BitConstants(0, 8, &commonResourcesIndices, 0);

            // make a function for setting cameras and global
            uint cameraValues[] = { 0 };
            commandBuffer->cmd->SetGraphicsRoot32BitConstants(1, 1, cameraValues, 0);

            auto& instances = view->viewWorld->instances;
            for (uint i = 0; i < instances.Size(); i++)
            {
                //make a function
                uint instanceValues[] = { i };
                commandBuffer->cmd->SetGraphicsRoot32BitConstants(2, 1, instanceValues, 0);
                //commandBuffer->cmd->SetGraphicsRootConstantBufferView(3, instances.GetGPUVirtualAddress(i));

                commandBuffer->cmd->DispatchMesh(1, 1, 1);
            }
        }


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

        tf::Task updateInstances = UpdateInstances(world, subflow);
        tf::Task updateMaterials = UpdateMaterials(world, subflow);
        tf::Task updateLights = UpdateLights(world, subflow);
        tf::Task updateCameras = UpdateCameras(world, subflow);

        tf::Task updloadInstancesBuffers = UploadInstancesBuffers(world, subflow);
        tf::Task updloadMeshesBuffers = UploadMeshesBuffers(world, subflow);
        tf::Task uploadShadersBuffers = UploadShadersBuffers(world, subflow);
        tf::Task uploadTexturesBuffers = UploadTexturesBuffers(world, subflow);

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

        //updateCameras.precede(particlesTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);
        updateInstances.precede(skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);
        updateInstances.precede(updateMaterials, updloadInstancesBuffers, updloadMeshesBuffers, uploadShadersBuffers, uploadTexturesBuffers);
        //uploadTask.succeed(updloadInstancesBuffers, updloadMeshesBuffers, uploadShadersBuffers, uploadTexturesBuffers);

        presentTask.succeed(updateInstances, updateMaterials, updloadInstancesBuffers, updloadMeshesBuffers, uploadShadersBuffers, uploadTexturesBuffers);
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

        uint meshesCount = world.CountQuery(Components::Mesh::mask, 0);
        GlobalResources::instance->meshes.Reserve(meshesCount);

        uint texturesCount = world.CountQuery(Components::Texture::mask, 0);
        GlobalResources::instance->textures.Reserve(texturesCount);

        uint shadersCount = world.CountQuery(Components::Shader::mask, 0);
        GlobalResources::instance->shaders.Reserve(shadersCount);

        
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

                    Components::Instance& instanceCmp = queryResult[i + subQuery].Get<Components::Instance>();
                    Components::Mesh& meshCmp = instanceCmp.mesh.Get();
                    Components::Material& materialCmp = instanceCmp.material.Get();
                    Components::Shader& shaderCmp = materialCmp.shader.Get();

                    uint meshIndex;
                    bool meshAdded = GlobalResources::instance->meshes.Add(meshCmp.id, meshIndex);
                    if (!GlobalResources::instance->meshes.GetLoaded(meshIndex)) 
                        continue;

                    uint shaderIndex;
                    bool shaderAdded = GlobalResources::instance->shaders.Add(shaderCmp.id, shaderIndex);
                    if (!GlobalResources::instance->shaders.GetLoaded(shaderIndex)) 
                        continue;

                    uint materialIndex;
                    bool materialAdded = frameWorld.materials.Add(World::Entity(instanceCmp.material.index), materialIndex);
                    if (!frameWorld.materials.GetLoaded(materialIndex))
                        continue;

                    // everything should be loaded to be able to draw the instance

                    Components::WorldMatrix& transformCmp = queryResult[i + subQuery].Get<Components::WorldMatrix>();

                    HLSL::Instance& instance = localInstances[instanceCount];
                    //instance.meshIndex = meshIndex;
                    //instance.materialIndex = materialIndex;
                    instance.worldMatrix = transformCmp.matrix;

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

                        uint shaderIndex;
                        bool shaderAdded = GlobalResources::instance->shaders.Contains(materialCmp.shader.Get().id, shaderIndex);

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
                                bool textureAdded = GlobalResources::instance->textures.Add(textureCmp.id, textureIndex);
                                if (!GlobalResources::instance->textures.GetLoaded(textureIndex)) 
                                    materialReady = false;
                                material.texturesSRV[texIndex] = GlobalResources::instance->textures.GetData(textureIndex).srv;
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
        // upload camera

        tf::Task task = subflow.emplace(
            [this, &world]()
            {
                ZoneScoped;
                uint queryIndex = world.Query(Components::Camera::mask, 0);

                uint entityCount = (uint)world.frameQueries[queryIndex].size();
                uint entityStep = 1;
                ViewWorld& frameWorld = viewWorld.Get();
                auto& queryResult = world.frameQueries[queryIndex];

                this->viewWorld->cameras.Clear();
                for (uint i = 0; i < entityCount; i++)
                {
                    auto& cam = queryResult[i].Get<Components::Camera>();
                    auto& trans = queryResult[i].Get<Components::WorldMatrix>();
                    float4x4 proj = MatrixPerspectiveFovLH(cam.fovY * (3.14f / 180.0f), float(this->resolution.x) / float(this->resolution.y), cam.nearClip, cam.farClip);
                    float4x4 viewProj = mul(inverse(trans.matrix), proj);
                    HLSL::Camera hlslcam;
                    hlslcam.viewProj = viewProj;
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
            }
        ).name("upload instances buffer");

        return task;
    }

    tf::Task UploadMeshesBuffers(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            []() 
            {
                for (uint i = 0; i < GlobalResources::instance->meshes.count; i++)
                {
                    auto& cpu = GlobalResources::instance->meshes.GetData(i);
                    auto& gpu = GlobalResources::instance->meshes.GetGPUData(i);
                    gpu.meshletOffset = cpu.meshletOffset;
                    gpu.meshletCount = cpu.meshletCount;
                }
                GlobalResources::instance->meshes.Upload();
            }
        ).name("upload meshes buffer");

        return task;
    }

    tf::Task UploadShadersBuffers(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            []()
            {
                for (uint i = 0; i < GlobalResources::instance->shaders.count; i++)
                {
                    auto& cpu = GlobalResources::instance->shaders.GetData(i);
                    auto& gpu = GlobalResources::instance->shaders.GetGPUData(i);
                    gpu.id = i;//cpu.something;
                }
                GlobalResources::instance->shaders.Upload();
            }
        ).name("upload shaders buffer");

        return task;
    }

    tf::Task UploadTexturesBuffers(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        tf::Task task = subflow.emplace(
            []()
            {
                for (uint i = 0; i < GlobalResources::instance->textures.count; i++)
                {
                    auto& cpu = GlobalResources::instance->textures.GetData(i);
                    auto& gpu = GlobalResources::instance->textures.GetGPUData(i);
                    gpu.index = cpu.srv.offset;
                }
                GlobalResources::instance->textures.Upload();
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
    GlobalResources globalResources;
    Upload upload;
    MainView mainView;
    EditorView editorView;

    void On(IOs::WindowInformation& window)
    {
        globalResources.On();
        mainView.On(window);
        editorView.On(window);
        upload.On(nullptr, false, "upload");
    }
    
    void Off()
    {
        upload.Off();
        editorView.Off();
        mainView.Off();
        globalResources.Off();
    }

    void Schedule(World& world, tf::Subflow& subflow)
    {
        ZoneScoped;

        auto mainViewEndTask = mainView.Schedule(world, subflow);
        auto editorViewEndTask = editorView.Schedule(world, subflow);

        SUBTASKPASS(upload);
        SUBTASKRENDERER(ExecuteFrame);
        SUBTASKRENDERER(WaitFrame);
        SUBTASKRENDERER(PresentFrame);

        ExecuteFrame.succeed(mainViewEndTask, editorViewEndTask, uploadTask);
        ExecuteFrame.precede(WaitFrame);
        WaitFrame.precede(PresentFrame);
    }

    void ScheduleLoading(tf::Subflow& subflow)
    {
        ZoneScoped;
        uint loadingLeft = GlobalResources::instance->shaders.maxLoading;
        for (auto& item : GlobalResources::instance->shaders)
        {
            auto& shader = GlobalResources::instance->shaders.GetData(item.first);
            if (!GlobalResources::instance->shaders.GetLoaded(item.second) || shader.NeedReload())
            {
                assetID i = item.first;
                // load shader
                tf::Task pass = subflow.emplace([this, i]() {this->LoadShaders(i); }).name("LoadShaders");
                loadingLeft--;
            }
            if (loadingLeft == 0)
                break;
        }
        loadingLeft = GlobalResources::instance->meshes.maxLoading;
        for (auto& item : GlobalResources::instance->meshes)
        {
            auto& shader = GlobalResources::instance->meshes.GetData(item.first);
            if (!GlobalResources::instance->meshes.GetLoaded(item.second))
            {
                assetID i = item.first;
                // load mesh
                tf::Task pass = subflow.emplace([this, i]() {this->LoadMeshes(i); }).name("LoadMeshes");
                loadingLeft--;
            }
            if (loadingLeft == 0)
                break;
        }
        loadingLeft = GlobalResources::instance->textures.maxLoading;
        for (uint i = 0; i < GlobalResources::instance->textures.count; i++)
        {
            if (!GlobalResources::instance->textures.GetLoaded(i))
            {
                // load texture
                tf::Task pass = subflow.emplace([this, i]() {this->LoadTextures(i); }).name("LoadTextures");
                loadingLeft--;
            }
            if (loadingLeft == 0)
                break;
        }
    }

    // do that in backgroud task ?
    void LoadShaders(assetID i)
    {
        ZoneScoped;
        /*
        // shader compile work across the entire frame. but GlobalResources::instance->shaders storage vectors can be resize in the same time
        // so the ptr of auto& shadercan no longer point to something good anymore during or after the compilation
        auto& shader = GlobalResources::instance->shaders.GetData(i);
        bool compiled = ShaderLoader::instance->Load(shader, AssetLibrary::instance->Get(i));
        GlobalResources::instance->shaders.SetLoaded(i, compiled);
        */
        // so to be thread safe : do the compilation on a tmp Shader object and then copy the result after
        // and why not lock the shaders map ..
        Shader shader;
        bool compiled = ShaderLoader::instance->Load(shader, AssetLibrary::instance->Get(i));
        GlobalResources::instance->shaders.lock.lock();
        GlobalResources::instance->shaders.GetData(i) = shader;
        GlobalResources::instance->shaders.SetLoaded(i, compiled);
        GlobalResources::instance->shaders.lock.unlock();
    }
    void LoadMeshes(assetID i)
    {
        ZoneScoped;
        MeshLoader::Mesh mesh = MeshLoader::instance->Read(AssetLibrary::instance->Get(i));
        GlobalResources::instance->meshes.lock.lock();
        Mesh mesh2 = GlobalResources::instance->meshStorage.Add(mesh);
        GlobalResources::instance->meshes.GetData(i) = mesh2;
        GlobalResources::instance->meshes.SetLoaded(i, true);
        GlobalResources::instance->meshes.lock.unlock();
    }
    void LoadTextures(uint i)
    {
        ZoneScoped;
        IOs::Log("texture");
        GlobalResources::instance->textures.GetData(i);
        GlobalResources::instance->textures.SetLoaded(i, true);
    }

    void ExecuteFrame()
    {
        ZoneScoped;
        HRESULT hr;

        upload.Execute();
        mainView.Execute();
        editorView.Execute();
    }

    void WaitFrame()
    {
        ZoneScoped;
        Resource::CleanUploadResources();

        //Sleep(500);

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
