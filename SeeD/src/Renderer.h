#pragma once


struct CullingContext
{
    StructuredUploadBuffer<Camera> camera;
    StructuredUploadBuffer<Instance> instancesInView;
    StructuredUploadBuffer<Light> lights;
    TLAS tlas;
};

class RendererWorld
{
public:
    StructuredUploadBuffer<Camera> cameras;
    StructuredUploadBuffer<Instance> instances;
    StructuredUploadBuffer<Instance> instancesGPU; // only for instances created on GPU
    StructuredUploadBuffer<Mesh> meshes;
    StructuredUploadBuffer<Material> materials;
    StructuredUploadBuffer<Shader> shaders;
    StructuredUploadBuffer<Light> lights;
};



class View
{
public:
    PerFrame<RendererWorld> rendererWorld;
    CullingContext cullingContext;
    uint2 resolution;

    virtual void Start(IOs::WindowInformation* window) = 0;
    virtual void Stop() = 0;
    virtual tf::Task Schedule(World* world, tf::Subflow& subflow) = 0;
    virtual void Execute() = 0;
};

class Pass
{
public:
    PerFrame<CommandBuffer> commandBuffer;
    String name;

    void Start(bool asyncCompute, String _name)
    {
        ZoneScoped;
        name = _name;

        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            D3D12_COMMAND_LIST_TYPE type = asyncCompute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;
            commandBuffer.Get(i)->queue = asyncCompute ? GPU::instance->computeQueue : GPU::instance->graphicQueue;
            HRESULT hr = GPU::instance->device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandBuffer.Get(i)->cmdAlloc));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = commandBuffer.Get(i)->cmdAlloc->SetName(name.c_str());
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = GPU::instance->device->CreateCommandList(0, type, commandBuffer.Get(i)->cmdAlloc, NULL, IID_PPV_ARGS(&commandBuffer.Get(i)->cmd));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
            }
            hr = commandBuffer.Get(i)->cmd->SetName(name.c_str());
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
        }
    }

    void Stop()
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
            IOs::Log(L"{} OPEN !!", name.c_str());
        ID3D12CommandQueue* commandQueue = GPU::instance->graphicQueue;
        commandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&commandBuffer->cmd);
        commandQueue->Signal(commandBuffer->passEnd.fence, ++commandBuffer->passEnd.fenceValue);
    }

    virtual void Render(View* view) = 0;
};

class Skinning : public Pass
{
public:
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
    void Render(View* view) override
    {
        ZoneScoped;
        Open();
        Close();
    }
};

class Forward : public Pass
{
public:
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

    void Start(IOs::WindowInformation* window) override
    {
        resolution = window->windowResolution;

        skinning.Start(false, L"skinning");
        particles.Start(false, L"particles");
        spawning.Start(false, L"spawning");
        culling.Start(false, L"culling");
        zPrepass.Start(false, L"zPrepass");
        gBuffers.Start(false, L"gBuffers");
        lighting.Start(false, L"lighting");
        forward.Start(false, L"forward");
        postProcess.Start(false, L"postProcess");
        present.Start(false, L"present");
    }

    void Stop() override
    {
        skinning.Stop();
        particles.Stop();
        spawning.Stop();
        culling.Stop();
        zPrepass.Stop();
        gBuffers.Stop();
        lighting.Stop();
        forward.Stop();
        postProcess.Stop();
        present.Stop();
    }

    tf::Task Schedule(World* world, tf::Subflow& subflow) override
    {
        ZoneScoped;

        SUBTASKRENDERERWORLD(UpdateMeshes);
        SUBTASKRENDERERWORLD(UpdateMaterials);
        SUBTASKRENDERERWORLD(UpdateShaders);
        SUBTASKRENDERERWORLD(UpdateInstances);
        SUBTASKRENDERERWORLD(UpdateLights);
        SUBTASKRENDERERWORLD(UpdateCameras);

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

        UpdateInstances.succeed(UpdateMeshes, UpdateMaterials, UpdateShaders);
        UpdateInstances.precede(skinningTask, particlesTask, spawningTask, cullingTask, zPrepassTask, gBuffersTask, lightingTask, forwardTask, postProcessTask);
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

    void UpdateMeshes(World* world)
    {
        ZoneScoped;
        // parallel for meshes
            // load mesh from disk
            // upload mesh
            // update BLAS
    }

    void UpdateMaterials(World* world)
    {
        ZoneScoped;
        // parallel for materials
            // load material from disk
            // load textures from disk
            // upload textures
            // upload materials
    }

    void UpdateShaders(World* world)
    {
        ZoneScoped;
        // parallel for shaders
            // compile shader
    }

    void UpdateInstances(World* world)
    {
        ZoneScoped;
        rendererWorld->instances.Clear();
        World::Query query;
        query.Add(Components::Instance::mask);
        world->ParallelFor <
            [](void* data, World::Entity entity)
            {
                RendererWorld* rendererWorld = (RendererWorld*)data;

                Components::Instance* instanceCmp = entity.Get<Components::Instance>();
                Components::Mesh* meshCmp = instanceCmp->mesh;
                Components::Material* materialCmp = instanceCmp->material;
                Components::Shader* shaderCmp = materialCmp->shader;

                Shader shader{};
                Material material{};
                Mesh mesh{};
                Instance instance{};

                uint shaderIndex = rendererWorld->shaders.AddUnique(shader);
                material.shaderIndex = shaderIndex;

                uint materialIndex = rendererWorld->materials.AddUnique(material);
                instance.materialIndex = materialIndex;

                uint meshIndex = rendererWorld->meshes.AddUnique(mesh);
                instance.meshIndex = meshIndex;

                rendererWorld->instances.Add(instance);

                // if in range (depending on distance and BC size)
                    // Add to TLAS
                // count instances with shader
            }
        >
            (query, (void*)&rendererWorld);

        // create execute indirect buckets
        // update TLAS
    }

    void UpdateLights(World* world)
    {
        ZoneScoped;
        // upload lights
    }

    void UpdateCameras(World* world)
    {
        ZoneScoped;
        // upload camera
    }
};


class Editor : public Pass
{
public:
    void Render(View* view) override
    {
        ZoneScoped;
        Open();

        commandBuffer->cmd->OMSetRenderTargets(1, &GPU::instance->backBuffer->rtv.handle, false, nullptr);

        UI::instance->FrameStop(commandBuffer->cmd);

        GPU::instance->backBuffer->Transition(commandBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

        Profiler::instance->EndProfilerFrame(commandBuffer.Get());
        Close();
    }
};

class EditorView : View
{
public:
    Editor editor;

    void Start(IOs::WindowInformation* window) override
    {
        resolution = window->windowResolution;

        editor.Start(false, L"editor");
    }

    void Stop() override
    {
        editor.Stop();
    }

    tf::Task Schedule(World* world, tf::Subflow& subflow) override
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
    MainView mainView;
    EditorView editorView;

    PerFrame<Resource> backBuffer;

    void Start(IOs::WindowInformation* window)
    {
        mainView.Start(window);
        editorView.Start(window);
    }
    
    void Stop()
    {
        mainView.Stop();
        editorView.Stop();
    }

    void Schedule(World* world, tf::Subflow& subflow)
    {
        ZoneScoped;

        auto mainViewEndTask = mainView.Schedule(world, subflow);
        auto editorViewEndTask = editorView.Schedule(world, subflow);

        SUBTASKRENDERER(WaitFrame);
        SUBTASKRENDERER(ExecuteFrame);

        WaitFrame.succeed(mainViewEndTask, editorViewEndTask);
        WaitFrame.precede(ExecuteFrame);
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

    void ExecuteFrame()
    {
        ZoneScoped;
        HRESULT hr;

        mainView.Execute();
        editorView.Execute();

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