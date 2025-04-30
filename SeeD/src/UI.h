#pragma once

enum class PropertyTypes : int
{
    _none,
    _int,
    _uint,
    _float,
    _float2,
    _float3,
    _float4,
    _quaternion,
    _float4x4,
    _assetID,
    _Handle
};
bool KnownType(String typeName)
{
    if (typeName == "int")
        return true;
    else if (typeName == "uint")
        return true;
    else if (typeName == "float")
        return true;
    else if (typeName == "float2")
        return true;
    else if (typeName == "float3")
        return true;
    else if (typeName == "float4")
        return true;
    else if (typeName == "quaternion")
        return true;
    else if (typeName == "float4x4")
        return true;
    else if (typeName == "assetID")
        return true;
    else if (typeName == "Handle")
        return true;
    return false;
}
struct MemberInfoParse
{
    String name;
    String dataType;
    String dataTemplateType;
    String dataCount;
};
struct ComponentInfoParse
{
    String name;
    std::vector<MemberInfoParse> members;
};
struct MemberInfo
{
    String name;
    PropertyTypes dataType;
    Components::Mask dataTemplateType;
    int dataCount;
    int offset;
};
struct ComponentInfo
{
    String name;       // Member name
    Components::Mask mask;
    std::vector<MemberInfo> members;
};
std::vector<ComponentInfo> knownComponents;

class EditorWindow
{
public:
    EditorWindow(String _name)
    {
        name = _name;
        guiWindows.push_back(this);
    };
    static std::vector<EditorWindow*> guiWindows;
    bool isOpen = false;
    String name;
    static void UpdateWindows()
    {
        if (IOs::instance->keys.pressed[VK_TAB])
            editorState.show = !editorState.show;

        if (editorState.show)
        {
            for (uint i = 0; i < guiWindows.size(); i++)
            {
                guiWindows[i]->Update();
            }
        }
        else
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::MenuItem(World::instance->playing ? "Stop" : "Play") || IOs::instance->keys.pressed[VK_F2])
                {
                    World::instance->playing = !World::instance->playing;
                }
                ImGui::Separator();

                //duplicate of profiler framerate info
                ImGui::Text("average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
                ImGui::Separator();

                ImGui::EndMainMenuBar();
            }
            //ImGui::End();
        }
    }

    void Open()
    {
        isOpen = true;
    }
    virtual void Update() = 0;
};
std::vector<EditorWindow*> EditorWindow::guiWindows;

class ProfilerWindow : public EditorWindow
{
public:
    ProfilerWindow() : EditorWindow("Profiler") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("Profiler", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }
        ImGui::Text("average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        ImGui::Text("instances %u | meshlets %u", Profiler::instance->frameData.instancesCount, Profiler::instance->frameData.meshletsCount);

        ImGui::Separator();

        for (uint j = 0; j < ARRAYSIZE(Profiler::instance->queueProfile); j++)
        {
            if (j == 0)
                ImGui::Text("Graphic");
            if (j == 1)
                ImGui::Text("Compute");
            if (j == 2)
                ImGui::Text("Copy");

            auto& prof = Profiler::instance->queueProfile[j];
            for (int i = 0; i < prof.entries.size(); i++)
            {
                Profiler::ProfileData& profile = prof.entries[i];
                if (profile.name == nullptr)
                    continue;

                double maxTime = 0.0;
                double avgTime = 0.0;
                UINT64 avgTimeSamples = 0;
                for (UINT i = 0; i < Profiler::ProfileData::FilterSize; ++i)
                {
                    if (profile.TimeSamples[i] <= 0.0)
                        continue;
                    maxTime = profile.TimeSamples[i] > maxTime ? profile.TimeSamples[i] : maxTime;
                    avgTime += profile.TimeSamples[i];
                    ++avgTimeSamples;
                }

                if (avgTimeSamples > 0)
                    avgTime /= double(avgTimeSamples);

                ImGui::Text("\t %s: %.2fms (%.2fms max)", profile.name, avgTime, maxTime);
            }
        }

        ImGui::End();
    }
};
ProfilerWindow profilerWindow;

class AssetLibraryWindow : public EditorWindow
{
public:
    AssetLibraryWindow() : EditorWindow("AssetLibrary") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("AssetLibrary", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }
        
        if (ImGui::Button("Clear"))
        {
            AssetLibrary::instance->map.clear();
        }

        char path[256];
        strcpy(path, AssetLibrary::instance->importPath.c_str());
        if (ImGui::InputText("Import path", path, 256))
        {
            AssetLibrary::instance->importPath = path;
        }
        ImGui::Separator();

        for (auto& item : AssetLibrary::instance->map)
        {
            ImGui::RadioButton("##radio", item.second.indexInVector != ~0);
            ImGui::SameLine();
            ImGui::Text("%ul %s", item.first, item.second.path.c_str());
        }

        ImGui::End();
    }
};
AssetLibraryWindow assetLibraryWindowWindow;

class HierarchyWindow : public EditorWindow
{
    struct TreeNode
    {
        World::Entity               entity;
        String                      name;
        TreeNode*                   parent;
        std::vector<TreeNode*>      childs;
        uint                        indexInParent;  // Maintaining this allows us to implement linear traversal more easily
    };

    TreeNode world;
    std::vector<TreeNode> nodes;

    int componentFilterIndex = 0;
    
    void TreeNodeSetOpen(TreeNode* node, bool open)
    {
        ImGui::GetStateStorage()->SetBool((ImGuiID)node->entity.id, open);
    }

    bool TreeNodeGetOpen(TreeNode* node)
    {
        return ImGui::GetStateStorage()->GetBool((ImGuiID)node->entity.id);
    }

    int TreeCloseAndUnselectChildNodes(TreeNode* node, ImGuiSelectionBasicStorage* selection, int depth = 0)
    {
        // Recursive close (the test for depth == 0 is because we call this on a node that was just closed!)
        int unselected_count = selection->Contains((ImGuiID)node->entity.id) ? 1 : 0;
        if (depth == 0 || TreeNodeGetOpen(node))
        {
            for (TreeNode* child : node->childs)
                unselected_count += TreeCloseAndUnselectChildNodes(child, selection, depth + 1);
            TreeNodeSetOpen(node, false);
        }

        // Select root node if any of its child was selected, otherwise unselect
        selection->SetItemSelected((ImGuiID)node->entity.id, (depth == 0 && unselected_count > 0));
        return unselected_count;
    }

    void DrawNode(TreeNode* node, ImGuiSelectionBasicStorage* selection)
    {
        ImGuiTreeNodeFlags tree_node_flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        tree_node_flags |= ImGuiTreeNodeFlags_NavLeftJumpsBackHere; // Enable pressing left to jump to parent
        if (node->childs.size() == 0)
            tree_node_flags |= ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_Leaf;
        if (selection->Contains((ImGuiID)node->entity.id))
            tree_node_flags |= ImGuiTreeNodeFlags_Selected;
        if(node == &world)
            tree_node_flags |= ImGuiTreeNodeFlags_DefaultOpen;

        // Using SetNextItemStorageID() to specify storage id, so we can easily peek into
        // the storage holding open/close stage, using our TreeNodeGetOpen/TreeNodeSetOpen() functions.
        ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)(intptr_t)node);
        ImGui::SetNextItemStorageID((ImGuiID)node->entity.id);
        if (ImGui::TreeNodeEx(node->name.c_str(), tree_node_flags))
        {
            for (TreeNode* child : node->childs)
                DrawNode(child, selection);
            ImGui::TreePop();
        }
        else if (ImGui::IsItemToggledOpen())
        {
            TreeCloseAndUnselectChildNodes(node, selection);
        }
    }

    void TreeSetAllInOpenNodes(TreeNode* node, ImGuiSelectionBasicStorage* selection, bool selected)
    {
        if (node->parent != NULL) // Root node isn't visible nor selectable in our scheme
            selection->SetItemSelected((ImGuiID)node->entity.id, selected);
        if (node->parent == NULL || TreeNodeGetOpen(node))
            for (TreeNode* child : node->childs)
                TreeSetAllInOpenNodes(child, selection, selected);
    }

    TreeNode* TreeGetNextNodeInVisibleOrder(TreeNode* curr_node, TreeNode* last_node)
    {
        // Reached last node
        if (curr_node == last_node)
            return NULL;

        // Recurse into childs. Query storage to tell if the node is open.
        if (curr_node->childs.size() > 0 && TreeNodeGetOpen(curr_node))
            return curr_node->childs[0];

        // Next sibling, then into our own parent
        while (curr_node->parent != NULL)
        {
            if (curr_node->indexInParent + 1 < curr_node->parent->childs.size())
                return curr_node->parent->childs[curr_node->indexInParent + 1];
            curr_node = curr_node->parent;
        }
        return NULL;
    }

    // Apply multi-selection requests
    void ApplySelectionRequests(ImGuiMultiSelectIO* ms_io, TreeNode* tree, ImGuiSelectionBasicStorage* selection)
    {
        for (ImGuiSelectionRequest& req : ms_io->Requests)
        {
            if (req.Type == ImGuiSelectionRequestType_SetAll)
            {
                if (req.Selected)
                    TreeSetAllInOpenNodes(tree, selection, req.Selected);
                else
                    selection->Clear();
            }
            else if (req.Type == ImGuiSelectionRequestType_SetRange)
            {
                TreeNode* first_node = (TreeNode*)(intptr_t)req.RangeFirstItem;
                TreeNode* last_node = (TreeNode*)(intptr_t)req.RangeLastItem;
                for (TreeNode* node = first_node; node != NULL; node = TreeGetNextNodeInVisibleOrder(node, last_node))
                    selection->SetItemSelected((ImGuiID)node->entity.id, req.Selected);
            }
        }
    }

public:
    HierarchyWindow() : EditorWindow("HierarchyWindow") {}
    void Update() override final
    {
        ZoneScoped;

        if (editorState.dirtyHierarchy)
        {
            nodes.clear();

            world.entity = entityInvalid;
            world.name = "world";
            world.parent = NULL;
            world.childs.clear();
            world.indexInParent = 0;

            Components::Mask mask = Components::Entity::mask;
            if(knownComponents.size() > 0)
                mask = knownComponents[componentFilterIndex].mask;
            uint instanceQueryIndex = World::instance->Query(mask, 0);
            auto& result = World::instance->frameQueries[instanceQueryIndex];

            for (uint i = 0; i < result.size(); i++)
            {
                TreeNode newNode;
                newNode.entity = { result[i].Get<Components::Entity>().index };
                if (result[i].Has<Components::Name>())
                {
                    newNode.name = std::format("{}{}", result[i].Get<Components::Name>().name, i);
                }
                else
                {
                    newNode.name = std::format("{}{}", "un-name", i);
                }
                newNode.parent = &world;
                nodes.push_back(newNode);
            }

            for (uint i = 0; i < nodes.size(); i++)
            {
                TreeNode& node = nodes[i];
                
                bool hasParent = result[i].Has<Components::Parent>();
                if (!hasParent)
                {
                        node.parent = &world;
                }
                else
                {
                    auto& pent = result[i].Get<Components::Parent>().entity;
                    if (!pent.IsValid())// || !World::Entity(pent.Get().index).Has<Components::Instance>())
                    {
                        node.parent = &world;
                    }
                    else
                    {
                        World::Entity parent = { pent.Get().index };
                        for (uint j = 0; j < result.size(); j++)
                        {
                            TreeNode& nodeParent = nodes[j];
                            if (nodeParent.entity == parent)
                            {
                                node.parent = &nodeParent;
                            }
                        }
                    }
                }

                node.indexInParent = node.parent->childs.size();
                node.parent->childs.push_back(&node);
            }
            editorState.dirtyHierarchy = false;
        }

        if (!ImGui::Begin("HierarchyWindow", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }
        editorState.dirtyHierarchy |= ImGui::Combo("##Filter", &componentFilterIndex, [](void* data, int n) { return (*(std::vector<ComponentInfo>*)data)[n].name.c_str(); }, (void*)&knownComponents, knownComponents.size());
        ImGui::SameLine();
        if(ImGui::Button("new Entity"))
        {
            World::Entity ent;
            ent.Make(knownComponents[componentFilterIndex].mask);
            editorState.dirtyHierarchy |= true;
        }
        {
            static ImGuiSelectionBasicStorage selection;
            if (editorState.selectedObject == entityInvalid)
                selection.Clear();
            if (world.childs.size() > 0 && ImGui::BeginChild("##Tree", ImVec2(-FLT_MIN, -FLT_MIN), ImGuiChildFlags_FrameStyle))
            {
                ImGuiMultiSelectFlags ms_flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect2d;
                ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ms_flags, selection.Size, -1);
                ApplySelectionRequests(ms_io, &world, &selection);
                DrawNode(&world, &selection);
                ms_io = ImGui::EndMultiSelect();
                ApplySelectionRequests(ms_io, &world, &selection);

                void* it = NULL;
                ImGuiID id = 0;
                selection.GetNextSelectedItem(&it, &id);
                if (id != 0)
                {
                    editorState.selectedObject = { id };
                }

                ImGui::EndChild();
            }
        }

        ImGui::End();
    }
};
HierarchyWindow hierarchyWindow;

class GPUResourcesWindow : public EditorWindow
{
    //bool openDemo;
public:
    GPUResourcesWindow() : EditorWindow("GPUResources") {}
    void Update() override final
    {
        ZoneScoped;

        //ImGui::ShowDemoWindow(&openDemo);

        if (!ImGui::Begin("GPUResources", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        const std::lock_guard<std::mutex> lock(Resource::lock);

        const char* names[] = { "Resources", "Content" };
        static Resource selectedResource = {};
        static int mip = 0;
        bool selected;

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::BeginGroup();
        const ImGuiWindowFlags child_flags = ImGuiWindowFlags_MenuBar;
        ImGuiID child_id = ImGui::GetID((void*)(intptr_t)0);
        const bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(200, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders, child_flags);
        for (int i = 0; i < Resource::allResources.size(); i++)
        {

            ImGui::PushID(i);
            selected = selectedResource.allocation != nullptr && selectedResource.allocation == Resource::allResources[i].allocation;
            if (selected)
            {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), Resource::allResourcesNames[i].c_str());
            }
            else
            {
                ImGui::Selectable(Resource::allResourcesNames[i].c_str(), &selected);
                if (selected)
                    selectedResource = Resource::allResources[i];
            }
            ImGui::SameLine();
            if(Resource::allResources[i].GetResource())
                ImGui::Text("%u", Resource::allResources[i].GetResource()->GetDesc().Width);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        child_id = ImGui::GetID((void*)(intptr_t)1);
        ImGui::BeginChild(child_id, ImGui::GetContentRegionAvail(), ImGuiChildFlags_Borders, child_flags);
        if (selectedResource.GetResource() != nullptr)
        {
            ImGuiIO& io = ImGui::GetIO();
            auto desc = selectedResource.GetResource()->GetDesc();
            float textureW = (float)desc.Width;
            float textureH = (float)desc.Height;
            float ratio = textureW / textureH;
            ImVec2 img_sz = ImGui::GetContentRegionAvail();
            img_sz.y = img_sz.x / ratio;
            {
                ImGui::Text("%.0fx%.0f", textureW, textureH);
                ImGui::SameLine();
                ImGui::SliderInt("mip", &mip, 0, desc.MipLevels-1);

                seedAssert(mip < desc.MipLevels);

                // Create texture view
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
                ZeroMemory(&srvDesc, sizeof(srvDesc));
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = -1;// desc.MipLevels;
                srvDesc.Texture2D.MostDetailedMip = mip;
                srvDesc.Texture2D.PlaneSlice = 0;
                srvDesc.Texture2D.ResourceMinLODClamp = 0;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                if (srvDesc.Format == DXGI_FORMAT_D32_FLOAT) srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
                GPU::instance->device->CreateShaderResourceView(selectedResource.GetResource(), &srvDesc, UI::instance->imgCPUHandle);
                ImTextureID my_tex_id = UI::instance->imgGPUHandle.ptr;

                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 uv_min = ImVec2(0.0f, 0.0f);                 // Top-left
                ImVec2 uv_max = ImVec2(1.0f, 1.0f);                 // Lower-right
                ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);   // No tint
                ImVec4 border_col = ImGui::GetStyleColorVec4(ImGuiCol_Border);
                child_id = ImGui::GetID((void*)(intptr_t)2);
                ImGui::Image(my_tex_id, img_sz, uv_min, uv_max, tint_col, border_col);
                if (ImGui::BeginItemTooltip())
                {
                    ImVec2 imgPos = ImVec2(pos.x, pos.y);

                    float region_sz = 16.0f;
                    float region_x = io.MousePos.x - pos.x - region_sz * 0.5f;
                    float region_y = io.MousePos.y - pos.y - region_sz * 0.5f;
                    float zoom = 16.0f;
                    if (region_x < 0.0f) { region_x = 0.0f; }
                    else if (region_x > img_sz.x - region_sz) { region_x = img_sz.x - region_sz; }
                    if (region_y < 0.0f) { region_y = 0.0f; }
                    else if (region_y > img_sz.y - region_sz) { region_y = img_sz.y - region_sz; }
                    ImGui::Text("Min: (%.2f, %.2f)", region_x, region_y);
                    ImGui::Text("Max: (%.2f, %.2f)", region_x + region_sz, region_y + region_sz);
                    ImVec2 uv0 = ImVec2((region_x) / img_sz.x, (region_y) / img_sz.y);
                    ImVec2 uv1 = ImVec2((region_x + region_sz) / img_sz.x, (region_y + region_sz) / img_sz.y);
                    ImGui::Image(my_tex_id, ImVec2(region_sz * zoom, region_sz * zoom), uv0, uv1, tint_col, border_col);
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::End();
    }
};
GPUResourcesWindow gpuResourcesWindow;

#include "Shaders/structs.hlsl"
class OptionWindow : public EditorWindow
{
public:
    OptionWindow() : EditorWindow("Option") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("Option", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("stopFrustumUpdate", &options.stopFrustumUpdate);
        ImGui::Checkbox("stopBufferUpload", &options.stopBufferUpload);
        ImGui::Checkbox("stepMotion", &options.stepMotion);
        ImGui::Checkbox("shaderHotReload", &options.shaderReload);

        ImGui::End();
    }
};
OptionWindow optionWindow;

class PostProcessWindow : public EditorWindow
{
public:
    PostProcessWindow() : EditorWindow("PostProcess") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("PostProcessWindow", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        ImGui::SliderFloat("expoMul", &Renderer::instance->mainView.postProcess.ppparams.expoMul, 0, 8);
        ImGui::SliderFloat("expoAdd", &Renderer::instance->mainView.postProcess.ppparams.expoAdd, -1, 1);
        ImGui::SliderFloat("P", &Renderer::instance->mainView.postProcess.ppparams.P, 0, 2);
        ImGui::SliderFloat("a", &Renderer::instance->mainView.postProcess.ppparams.a, 0, 2);
        ImGui::SliderFloat("m", &Renderer::instance->mainView.postProcess.ppparams.m, 0, 2);
        ImGui::SliderFloat("l", &Renderer::instance->mainView.postProcess.ppparams.l, 0, 2);
        ImGui::SliderFloat("c", &Renderer::instance->mainView.postProcess.ppparams.c, 0, 3);
        ImGui::SliderFloat("b", &Renderer::instance->mainView.postProcess.ppparams.b, 0, 1);

        ImGui::End();
    }
};
PostProcessWindow postProcessWindow;

class HandlePickingWindow : public EditorWindow
{
    struct HandleEntry
    {
        int handle;
        String name;
    };
    std::vector<HandleEntry> entries;
    
    void* handle;
    World::Entity selectedEntity = entityInvalid;
public:
    HandlePickingWindow() : EditorWindow("HandlePickingWindow") { isOpen = false; }
    void Update() override final
    {
        ZoneScoped;

        if (handle == nullptr)
            return;

        if (!ImGui::Begin("Handle Picking", &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::BeginGroup();
        const ImGuiWindowFlags child_flags = ImGuiWindowFlags_MenuBar;
        ImGuiID child_id = ImGui::GetID((void*)(intptr_t)0);
        const bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - 23), ImGuiChildFlags_Borders, child_flags);
        for (int i = 0; i < entries.size(); i++)
        {
            ImGui::PushID(i);
            bool selected;
            selected = selectedEntity.id == entries[i].handle;
            if (selected)
            {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), entries[i].name.c_str());
            }
            else
            {
                ImGui::Selectable(entries[i].name.c_str(), &selected);
                if (selected)
                    selectedEntity.id = entries[i].handle;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        if (ImGui::Button("Select"))
        {
            *(int*)handle = selectedEntity.id;
            handle = nullptr;
            isOpen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            handle = nullptr;
            isOpen = false;
        }

        ImGui::End();
    }

    void Open(Components::Mask mask, void* _handle)
    {
        uint resCount = World::instance->CountQuery(mask, 0);
        uint instanceQueryIndex = World::instance->Query(mask, 0);
        auto& result = World::instance->frameQueries[instanceQueryIndex];

        entries.clear();
        for (uint i = 0; i < result.size(); i++)
        {
            HandleEntry entry;
            World::Entity entity = { result[i].Get<Components::Entity>().index };
            if (entity.Has<Components::Name>())
            {
                entry.name = entity.Get<Components::Name>().name;
            }
            else
            {
                entry.name = "toto";
            }
            entry.handle = entity.id;
            entries.push_back(entry);
        }
        handle = _handle;
        selectedEntity = *(int*)handle;
        isOpen = true;
    }
};
HandlePickingWindow handlePickingWindow;

#include "ComponentsUIMetaData.h"
class PropertyWindow : public EditorWindow
{
    int componentAddIndex = 0;
    unsigned int getFirstSetBitPos(int n)
    {
        return log2(n & -n) + 1;
    }
public:
    PropertyWindow() : EditorWindow("PropertyWindow") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("PropertyWindow", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Generate"))
        {
            ParseCpp("G:\\Work\\Dev\\SeeD\\SeeD\\src\\World.h");
        }
        
        InitKnownComponents();

        if (editorState.selectedObject != entityInvalid)
        {
            uint pushID = 0;
            for (uint i = 0; i < knownComponents.size(); i++)
            {
                if ((editorState.selectedObject.GetMask() & knownComponents[i].mask) != 0)
                {   
                    auto& metaData = knownComponents[i];
                    char* cmpData = editorState.selectedObject.Get(Components::MaskToBucket(metaData.mask));
                    if (ImGui::CollapsingHeader(metaData.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        for (uint memberIndex = 0; memberIndex < metaData.members.size(); memberIndex++)
                        {
                            auto& m = metaData.members[memberIndex];
                            char* data = cmpData + m.offset;
                            ImGui::Text(m.name.c_str());
                            ImGui::SameLine();
                            for (uint dc = 0; dc < m.dataCount; dc++)
                            {
                                ImGui::PushID(pushID++);
                                switch (m.dataType)
                                {
                                case PropertyTypes::_int:
                                case PropertyTypes::_uint:
                                    ImGui::InputInt("", &((int*)data)[dc]);
                                    break;
                                case PropertyTypes::_float:
                                    ImGui::InputFloat("", &((float*)data)[dc]);
                                    break;
                                case PropertyTypes::_float2:
                                    ImGui::InputFloat2("", &((float*)data)[dc]);
                                    break;
                                case PropertyTypes::_float3:
                                    ImGui::InputFloat3("", &((float*)data)[dc]);
                                    break;
                                case PropertyTypes::_float4:
                                    ImGui::InputFloat4("", &((float*)data)[dc]);
                                    break;
                                case PropertyTypes::_quaternion:
                                    ImGui::InputFloat4("", &((float*)data)[dc]);
                                    break;
                                case PropertyTypes::_float4x4:
                                {
                                    float4x4& mat = ((float4x4*)data)[dc];
                                    ImGui::InputFloat4("x", (float*)&mat[0]);
                                    ImGui::InputFloat4("y", (float*)&mat[1]);
                                    ImGui::InputFloat4("z", (float*)&mat[2]);
                                    ImGui::InputFloat4("pos", (float*)&mat[3]);
                                }
                                    break;
                                case PropertyTypes::_assetID:
                                    ImGui::InputInt("", &((int*)data)[dc]);
                                    break;
                                case PropertyTypes::_Handle:
                                {
                                    World::Entity handleTarget = ((int*)data)[dc];
                                    String handleName = "toto";
                                    if (handleTarget != entityInvalid && handleTarget.Has<Components::Name>())
                                    {
                                        handleName = handleTarget.Get<Components::Name>().name;
                                    }
                                    if (ImGui::SmallButton("o"))
                                    {
                                        handlePickingWindow.Open(m.dataTemplateType, &((int*)data)[dc]);
                                    }
                                    ImGui::SameLine();
                                    ImGui::Text(handleName.c_str());
                                }
                                    break;
                                default:
                                    break;
                                }
                                ImGui::PopID();
                            }
                        }
                    }
                }
            }
            
            ImGui::Separator();
            editorState.dirtyHierarchy |= ImGui::Combo("##AddComp", &componentAddIndex, [](void* data, int n) { return (*(std::vector<ComponentInfo>*)data)[n].name.c_str(); }, (void*)&knownComponents, knownComponents.size());
            ImGui::SameLine();
            if (ImGui::Button("add"))
            {
                editorState.selectedObject.Add(knownComponents[componentAddIndex].mask);
            }
        }

        ImGui::End();
    }

    String FormatMetaData(ComponentInfoParse cmpInfo, uint& cmpIndex)
    {
        String out = std::format( "knownComponents.push_back(\n \t{{ \"{0}\", Components::{0}::mask, \n \t\t{{\n",  cmpInfo.name.c_str());

        for (uint i = 0; i < cmpInfo.members.size(); i++)
        {
            auto& m = cmpInfo.members[i];
            out += std::format("\t\t\t{{ \"{0}\", PropertyTypes::_{1}, {2}, {3}, offsetof(Components::{4}, {0}) }},\n", m.name.c_str(), m.dataType.c_str(), m.dataTemplateType.c_str(), m.dataCount.c_str(), cmpInfo.name.c_str());
        }

        out += "\t\t}\n \t}";
        out += "); \n";

        cmpIndex++;
        return out;
    }
    String FormatComponentsArray(std::vector<String>& componentsInfo)
    {
        String out = "ComponentInfo knownComponents[] = \n{\n";

        for (uint i = 0; i < componentsInfo.size(); i++)
        {
            auto& name = componentsInfo[i];
            out += std::format("\t{}MetaData,\n", name.c_str());
        }

        out += "};\n";

        return out;
    }

    void ParseCpp(String path)
    {

        std::ifstream fin(path);
        std::ofstream fout("G:\\Work\\Dev\\SeeD\\SeeD\\src\\ComponentsUIMetaData.h");
        if (fin.is_open() && fout.is_open())
        {
            fout << "#pragma once" << std::endl;

            //fout << "std::vector<ComponentInfo> knownComponents;" << std::endl;
            fout << "void InitKnownComponents() { \n" << std::endl;
            fout << "static bool initialized = false;\n" << std::endl;
            fout << "if(initialized) return;\n" << std::endl;
            fout << "initialized = true;\n" << std::endl;

            std::vector<String> componentsKnownNames;
            String line;
            uint cmpIndex = 0;
            while (getline(fin, line))
            {
                if (line.find(": ComponentBase<") != -1)
                {
                    auto tokens = line.Split(" ");
                    ComponentInfoParse cmpInfo;
                    cmpInfo.name = tokens[1];
                    while (line.find("};") == -1)
                    {
                        getline(fin, line);
                        tokens = line.Split(" ");
                        if (tokens.size() == 2)
                        {
                            String typeToken = tokens[0];
                            String templateTypeToken = "Components::Entity::mask"; // a BS component but one that is always here
                            int chevron = typeToken.find("<");
                            if (chevron != -1)
                            {
                                templateTypeToken = typeToken.substr(chevron + 1, typeToken.find(">") - chevron - 1);
                                templateTypeToken = std::format("Components::{0}::mask", templateTypeToken.c_str());
                                typeToken = typeToken.substr(0, chevron);
                            }
                            if (KnownType(typeToken))
                            {
                                MemberInfoParse info;
                                info.dataType = typeToken;
                                info.dataTemplateType = templateTypeToken;
                                info.name = tokens[1].substr(0, tokens[1].length()-1); //delete the ; at the end
                                int bracketIndex = info.name.find("[");
                                if (bracketIndex != -1)
                                {
                                    int bracketIndexOut = info.name.find("]");
                                    info.dataCount = info.name.substr(bracketIndex+1, bracketIndexOut - bracketIndex - 1);
                                    info.name = info.name.substr(0, bracketIndex);
                                }
                                else
                                {
                                    info.dataCount = "1";
                                }
                                cmpInfo.members.push_back(info);
                            }
                        }
                    }
                    fout << FormatMetaData(cmpInfo, cmpIndex);
                    componentsKnownNames.push_back(cmpInfo.name);
                }
            }

            fout << "}\n" << std::endl;
            //fout << FormatComponentsArray(componentsKnownNames);

            fin.close();
            fout.close();
        }
    }
};
PropertyWindow propertyWindow;

#include "../../Third/ImGuizmo-master/ImGuizmo.h"
class Guizmo : public EditorWindow
{
public:
    Guizmo() : EditorWindow("Guizmo") {}

    void SetWorldPositionRotationScale(World::Entity ent, float3 position, quaternion rotation, float3 scale)
    {
        Components::Transform& t = ent.Get<Components::Transform>();
        Components::Parent* par = 0;
        if (ent.Has<Components::Parent>())
        {
            par = &ent.Get<Components::Parent>();
        }
        if (par != NULL && par->entity.IsValid())
        {
            World::Entity parentEnt = par->entity.Get().index;
            float4x4 parentMatrix = ComputeWorldMatrix(parentEnt);
            float4x4 invParentMat = inverse(parentMatrix);

            float4x4 rotationMat;
            float4x4 translationMat;
            float4x4 scaleMat;

            translationMat = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, position.x, position.y, position.z, 1);
            rotationMat = float4x4(normalize(rotation));
            scaleMat = float4x4(scale.x, 0, 0, 0, 0, scale.y, 0, 0, 0, 0, scale.z, 0, 0, 0, 0, 1);

            float4x4 matrix = mul(scaleMat, mul(rotationMat, translationMat));

            float3x3 rotNormalized = mul(float3x3(matrix), inverse(float3x3(parentMatrix)));
            rotNormalized[0] = normalize(rotNormalized[0]);
            rotNormalized[1] = normalize(rotNormalized[1]);
            rotNormalized[2] = normalize(rotNormalized[2]);
            rotation = quaternion(rotNormalized);

            matrix = mul(matrix, invParentMat);

            scale = float3(length(matrix[0].xyz), length(matrix[1].xyz), length(matrix[2].xyz));
            position = matrix[3].xyz;
        }
        t.position = position;
        t.rotation = rotation;
        t.scale = scale;
    }

    void EditTransform(const float4x4& cameraView, const float4x4& cameraProj, World::Entity selectedObject)
    {
        static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
        static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);
        if (ImGui::IsKeyPressed(ImGuiKey_Z))
            mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_X))
            mCurrentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_C))
            mCurrentGizmoOperation = ImGuizmo::SCALE;
        if (ImGui::IsKeyPressed(ImGuiKey_V))
            mCurrentGizmoMode = mCurrentGizmoMode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        /*
        if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
            mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
            mCurrentGizmoOperation = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
            mCurrentGizmoOperation = ImGuizmo::SCALE;
        float matrixTranslation[3], matrixRotation[3], matrixScale[3];
        ImGuizmo::DecomposeMatrixToComponents(matrix.m16, matrixTranslation, matrixRotation, matrixScale);
        ImGui::InputFloat3("Tr", matrixTranslation, 3);
        ImGui::InputFloat3("Rt", matrixRotation, 3);
        ImGui::InputFloat3("Sc", matrixScale, 3);
        ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, matrix.m16);

        if (mCurrentGizmoOperation != ImGuizmo::SCALE)
        {
            if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
                mCurrentGizmoMode = ImGuizmo::LOCAL;
            ImGui::SameLine();
            if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
                mCurrentGizmoMode = ImGuizmo::WORLD;
        }
        static bool useSnap(false);
        if (ImGui::IsKeyPressed(83))
            useSnap = !useSnap;
        ImGui::Checkbox("", &useSnap);
        ImGui::SameLine();
        vec_t snap;
        switch (mCurrentGizmoOperation)
        {
        case ImGuizmo::TRANSLATE:
            snap = config.mSnapTranslation;
            ImGui::InputFloat3("Snap", &snap.x);
            break;
        case ImGuizmo::ROTATE:
            snap = config.mSnapRotation;
            ImGui::InputFloat("Angle Snap", &snap.x);
            break;
        case ImGuizmo::SCALE:
            snap = config.mSnapScale;
            ImGui::InputFloat("Scale Snap", &snap.x);
            break;
        }
        */

        ImGuizmo::BeginFrame();
        ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

        float4x4 worldMatrix = ComputeWorldMatrix(selectedObject);

        float view[16];
        store(inverse(cameraView), view);
        float proj[16];
        store(cameraProj, proj);
        float world[16];
        store(worldMatrix, world);

        bool manipulated = ImGuizmo::Manipulate(view, proj, mCurrentGizmoOperation, mCurrentGizmoMode, world, NULL, NULL);// useSnap ? &snap.x : NULL);

        if (manipulated)
        {
            float matrixTranslation[3], matrixRotation[3], matrixScale[3];
            ImGuizmo::DecomposeMatrixToComponents(world, matrixTranslation, matrixRotation, matrixScale);
            float4x4 worldMat;
            load(worldMat, world);
            worldMat[0] = normalize(worldMat[0]);
            worldMat[1] = normalize(worldMat[1]);
            worldMat[2] = normalize(worldMat[2]);
            SetWorldPositionRotationScale(selectedObject, float3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]), quaternion(float3x3(worldMat)), float3(matrixScale[0], matrixScale[1], matrixScale[2]));
        }

    }

    void Update() override final
    {
        ZoneScoped;
        if (editorState.selectedObject != entityInvalid && editorState.selectedObject.Has<Components::Transform>())
            EditTransform(editorState.cameraView, editorState.cameraProj, editorState.selectedObject);
    }
};
Guizmo guizmo;

class MainMenu : public EditorWindow
{
public:
    MainMenu() : EditorWindow("MainMenu") {}
    void Update() override final
    {
        ZoneScoped;

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::MenuItem(World::instance->playing ? "Stop" : "Play") || IOs::instance->keys.pressed[VK_F2])
            {
                World::instance->playing = !World::instance->playing;
            }
            ImGui::Separator();

            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save World"))
                {
                    World::instance->Save("Save");
                }
                if (ImGui::MenuItem("Load World"))
                {
                    editorState.selectedObject = {};
                    std::string file = "Save";
                    World::instance->Load(file);
                }
                if (ImGui::MenuItem("Clear World"))
                {
                    World::instance->Clear();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::BeginMenu("New Entity"))
                {
                    if (ImGui::MenuItem("Empty"))
                    {
                        World::Entity ent;
                        editorState.selectedObject = ent.Make(0);
                    }
                    if (ImGui::MenuItem("Light Directional"))
                    {
                        World::Entity ent;
                        editorState.selectedObject = ent.Make(Components::Light::mask | Components::Transform::mask, "Directional Light");
                        auto& trans = ent.Get<Components::Transform>();
                        trans.position = float3(0);
                        trans.rotation = quaternion(0.5, 0, 0, 0.866);
                        trans.scale = float3(1);
                        auto& light = ent.Get<Components::Light>();
                        light.color = float4(2, 1.75, 1.5, 1);
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Recompute Bounding Boxes"))
                {
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                for (uint i = 0; i < guiWindows.size(); i++)
                {
                    if (ImGui::MenuItem(guiWindows[i]->name.c_str()))
                    {
                        guiWindows[i]->Open();
                    }
                }
                ImGui::EndMenu();
            }

            //duplicate of profiler framerate info
            ImGui::Text("average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::Separator();

            ImGui::EndMainMenuBar();
        }
    }
};
MainMenu mainMenu;

/*
class AboutWindow : public EditorWindow
{
public:
    AboutWindow() : EditorWindow("About") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("About Dear ImGui", &isOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }
        //IMGUI_DEMO_MARKER("Tools/About Dear ImGui");
        ImGui::Text("Dear ImGui %s (%d)", IMGUI_VERSION, IMGUI_VERSION_NUM);

        ImGui::TextLinkOpenURL("Homepage", "https://github.com/ocornut/imgui");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("FAQ", "https://github.com/ocornut/imgui/blob/master/docs/FAQ.md");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("Wiki", "https://github.com/ocornut/imgui/wiki");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("Releases", "https://github.com/ocornut/imgui/releases");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("Funding", "https://github.com/ocornut/imgui/wiki/Funding");

        ImGui::Separator();
        ImGui::Text("By Omar Cornut and all Dear ImGui contributors.");
        ImGui::Text("Dear ImGui is licensed under the MIT License, see LICENSE for more information.");
        ImGui::Text("If your company uses this, please consider funding the project.");

        static bool show_config_info = false;
        ImGui::Checkbox("Config/Build Information", &show_config_info);
        if (show_config_info)
        {
            ImGuiIO& io = ImGui::GetIO();
            ImGuiStyle& style = ImGui::GetStyle();

            bool copy_to_clipboard = ImGui::Button("Copy to clipboard");
            ImVec2 child_size = ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 18);
            ImGui::BeginChild(ImGui::GetID("cfg_infos"), child_size, ImGuiChildFlags_FrameStyle);
            if (copy_to_clipboard)
            {
                ImGui::LogToClipboard();
                ImGui::LogText("```\n"); // Back quotes will make text appears without formatting when pasting on GitHub
            }

            ImGui::Text("Dear ImGui %s (%d)", IMGUI_VERSION, IMGUI_VERSION_NUM);
            ImGui::Separator();
            ImGui::Text("sizeof(size_t): %d, sizeof(ImDrawIdx): %d, sizeof(ImDrawVert): %d", (int)sizeof(size_t), (int)sizeof(ImDrawIdx), (int)sizeof(ImDrawVert));
            ImGui::Text("define: __cplusplus=%d", (int)__cplusplus);
#ifdef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_OBSOLETE_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_WIN32_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_WIN32_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_FILE_FUNCTIONS
            ImGui::Text("define: IMGUI_DISABLE_FILE_FUNCTIONS");
#endif
#ifdef IMGUI_DISABLE_DEFAULT_ALLOCATORS
            ImGui::Text("define: IMGUI_DISABLE_DEFAULT_ALLOCATORS");
#endif
#ifdef IMGUI_USE_BGRA_PACKED_COLOR
            ImGui::Text("define: IMGUI_USE_BGRA_PACKED_COLOR");
#endif
#ifdef _WIN32
            ImGui::Text("define: _WIN32");
#endif
#ifdef _WIN64
            ImGui::Text("define: _WIN64");
#endif
#ifdef __linux__
            ImGui::Text("define: __linux__");
#endif
#ifdef __APPLE__
            ImGui::Text("define: __APPLE__");
#endif
#ifdef _MSC_VER
            ImGui::Text("define: _MSC_VER=%d", _MSC_VER);
#endif
#ifdef _MSVC_LANG
            ImGui::Text("define: _MSVC_LANG=%d", (int)_MSVC_LANG);
#endif
#ifdef __MINGW32__
            ImGui::Text("define: __MINGW32__");
#endif
#ifdef __MINGW64__
            ImGui::Text("define: __MINGW64__");
#endif
#ifdef __GNUC__
            ImGui::Text("define: __GNUC__=%d", (int)__GNUC__);
#endif
#ifdef __clang_version__
            ImGui::Text("define: __clang_version__=%s", __clang_version__);
#endif
#ifdef __EMSCRIPTEN__
            ImGui::Text("define: __EMSCRIPTEN__");
            ImGui::Text("Emscripten: %d.%d.%d", __EMSCRIPTEN_major__, __EMSCRIPTEN_minor__, __EMSCRIPTEN_tiny__);
#endif
#ifdef IMGUI_HAS_VIEWPORT
            ImGui::Text("define: IMGUI_HAS_VIEWPORT");
#endif
#ifdef IMGUI_HAS_DOCK
            ImGui::Text("define: IMGUI_HAS_DOCK");
#endif
            ImGui::Separator();
            ImGui::Text("io.BackendPlatformName: %s", io.BackendPlatformName ? io.BackendPlatformName : "NULL");
            ImGui::Text("io.BackendRendererName: %s", io.BackendRendererName ? io.BackendRendererName : "NULL");
            ImGui::Text("io.ConfigFlags: 0x%08X", io.ConfigFlags);
            if (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard)        ImGui::Text(" NavEnableKeyboard");
            if (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad)         ImGui::Text(" NavEnableGamepad");
            if (io.ConfigFlags & ImGuiConfigFlags_NoMouse)                  ImGui::Text(" NoMouse");
            if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)      ImGui::Text(" NoMouseCursorChange");
            if (io.ConfigFlags & ImGuiConfigFlags_NoKeyboard)               ImGui::Text(" NoKeyboard");
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)            ImGui::Text(" DockingEnable");
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)          ImGui::Text(" ViewportsEnable");
            if (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)  ImGui::Text(" DpiEnableScaleViewports");
            if (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleFonts)      ImGui::Text(" DpiEnableScaleFonts");
            if (io.MouseDrawCursor)                                         ImGui::Text("io.MouseDrawCursor");
            if (io.ConfigViewportsNoAutoMerge)                              ImGui::Text("io.ConfigViewportsNoAutoMerge");
            if (io.ConfigViewportsNoTaskBarIcon)                            ImGui::Text("io.ConfigViewportsNoTaskBarIcon");
            if (io.ConfigViewportsNoDecoration)                             ImGui::Text("io.ConfigViewportsNoDecoration");
            if (io.ConfigViewportsNoDefaultParent)                          ImGui::Text("io.ConfigViewportsNoDefaultParent");
            if (io.ConfigDockingNoSplit)                                    ImGui::Text("io.ConfigDockingNoSplit");
            if (io.ConfigDockingWithShift)                                  ImGui::Text("io.ConfigDockingWithShift");
            if (io.ConfigDockingAlwaysTabBar)                               ImGui::Text("io.ConfigDockingAlwaysTabBar");
            if (io.ConfigDockingTransparentPayload)                         ImGui::Text("io.ConfigDockingTransparentPayload");
            if (io.ConfigMacOSXBehaviors)                                   ImGui::Text("io.ConfigMacOSXBehaviors");
            if (io.ConfigNavMoveSetMousePos)                                ImGui::Text("io.ConfigNavMoveSetMousePos");
            if (io.ConfigNavCaptureKeyboard)                                ImGui::Text("io.ConfigNavCaptureKeyboard");
            if (io.ConfigInputTextCursorBlink)                              ImGui::Text("io.ConfigInputTextCursorBlink");
            if (io.ConfigWindowsResizeFromEdges)                            ImGui::Text("io.ConfigWindowsResizeFromEdges");
            if (io.ConfigWindowsMoveFromTitleBarOnly)                       ImGui::Text("io.ConfigWindowsMoveFromTitleBarOnly");
            if (io.ConfigMemoryCompactTimer >= 0.0f)                        ImGui::Text("io.ConfigMemoryCompactTimer = %.1f", io.ConfigMemoryCompactTimer);
            ImGui::Text("io.BackendFlags: 0x%08X", io.BackendFlags);
            if (io.BackendFlags & ImGuiBackendFlags_HasGamepad)             ImGui::Text(" HasGamepad");
            if (io.BackendFlags & ImGuiBackendFlags_HasMouseCursors)        ImGui::Text(" HasMouseCursors");
            if (io.BackendFlags & ImGuiBackendFlags_HasSetMousePos)         ImGui::Text(" HasSetMousePos");
            if (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports)   ImGui::Text(" PlatformHasViewports");
            if (io.BackendFlags & ImGuiBackendFlags_HasMouseHoveredViewport)ImGui::Text(" HasMouseHoveredViewport");
            if (io.BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset)   ImGui::Text(" RendererHasVtxOffset");
            if (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports)   ImGui::Text(" RendererHasViewports");
            ImGui::Separator();
            ImGui::Text("io.Fonts: %d fonts, Flags: 0x%08X, TexSize: %d,%d", io.Fonts->Fonts.Size, io.Fonts->Flags, io.Fonts->TexWidth, io.Fonts->TexHeight);
            ImGui::Text("io.DisplaySize: %.2f,%.2f", io.DisplaySize.x, io.DisplaySize.y);
            ImGui::Text("io.DisplayFramebufferScale: %.2f,%.2f", io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            ImGui::Separator();
            ImGui::Text("style.WindowPadding: %.2f,%.2f", style.WindowPadding.x, style.WindowPadding.y);
            ImGui::Text("style.WindowBorderSize: %.2f", style.WindowBorderSize);
            ImGui::Text("style.FramePadding: %.2f,%.2f", style.FramePadding.x, style.FramePadding.y);
            ImGui::Text("style.FrameRounding: %.2f", style.FrameRounding);
            ImGui::Text("style.FrameBorderSize: %.2f", style.FrameBorderSize);
            ImGui::Text("style.ItemSpacing: %.2f,%.2f", style.ItemSpacing.x, style.ItemSpacing.y);
            ImGui::Text("style.ItemInnerSpacing: %.2f,%.2f", style.ItemInnerSpacing.x, style.ItemInnerSpacing.y);

            if (copy_to_clipboard)
            {
                ImGui::LogText("\n```\n");
                ImGui::LogFinish();
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
};
AboutWindow aboutWindow;
*/
