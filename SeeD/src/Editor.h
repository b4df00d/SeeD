
class ProfilerWindow : public EditorWindow
{
    enum Stat { Stat_Cur = 0, Stat_Avg, Stat_Min, Stat_Max };
    int statMode = Stat_Avg;

    struct Bar
    {
        const char* name;
        double start, length;
        double cur, mn, avg, mx;
    };

    double GatherBars(std::vector<Bar>* lanes)
    {
        double span = 0.0;
        for (uint q = 0; q < ARRAYSIZE(Profiler::instance->queueProfile); q++)
        {
            for (Profiler::ProfileData& profile : Profiler::instance->queueProfile[q].entries)
            {
                if (profile.name == nullptr || profile.name[0] == '\0')
                    continue;

                // CurrSample is where the NEXT frame will write, so the last one is just behind it
                UINT64 last = (profile.CurrSample + Profiler::ProfileData::FilterSize - 1) % Profiler::ProfileData::FilterSize;

                Bar bar = { profile.name, 0.0, 0.0, profile.TimeSamples[last], 0.0, 0.0, 0.0 };
                double startSum = 0.0;
                uint samples = 0;
                for (uint s = 0; s < Profiler::ProfileData::FilterSize; s++)
                {
                    double t = profile.TimeSamples[s];
                    if (t <= 0.0)
                        continue;
                    if (samples == 0) { bar.mn = t; bar.mx = t; }
                    else { bar.mn = min(bar.mn, t); bar.mx = max(bar.mx, t); }
                    bar.avg += t;
                    startSum += profile.StartSamples[s];
                    samples++;
                }
                if (samples == 0)
                    continue;

                bar.avg /= samples;
                if (statMode == Stat_Cur)
                {
                    if (bar.cur <= 0.0) // did not run last frame : no zone to show
                        continue;
                    bar.start = profile.StartSamples[last];
                    bar.length = bar.cur;
                }
                else
                {
                    bar.start = startSum / samples;
                    bar.length = statMode == Stat_Min ? bar.mn : (statMode == Stat_Max ? bar.mx : bar.avg);
                }

                span = max(span, bar.start + bar.length);
                lanes[q].push_back(bar);
            }
        }
        return span;
    }

    void DrawLanes(std::vector<Bar>* lanes, double span)
    {
        if (span <= 0.0)
        {
            ImGui::TextDisabled("No GPU timings yet.");
            return;
        }

        const char* queueNames[Profiler::PerQueueData::queueCount] = { "Graphic", "Compute", "Copy" };
        const float labelWidth = 62.0f;
        const float laneHeight = 20.0f;
        const float lanePadding = 3.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        for (uint q = 0; q < Profiler::PerQueueData::queueCount; q++)
        {
            double queueTotal = 0.0;
            for (const Bar& bar : lanes[q])
                queueTotal += bar.length;

            ImVec2 origin = ImGui::GetCursorScreenPos();
            float width = max(ImGui::GetContentRegionAvail().x - labelWidth, 1.0f);
            ImVec2 trackMin = ImVec2(origin.x + labelWidth, origin.y);
            ImVec2 trackMax = ImVec2(trackMin.x + width, origin.y + laneHeight);

            dl->AddText(ImVec2(origin.x, origin.y + 3.0f), IM_COL32(180, 180, 180, 255), queueNames[q]);
            dl->AddRectFilled(trackMin, trackMax, IM_COL32(32, 32, 32, 255));

            for (const Bar& bar : lanes[q])
            {
                float x0 = trackMin.x + width * (float)(bar.start / span);
                float x1 = trackMin.x + width * (float)((bar.start + bar.length) / span);
                x1 = max(x1, x0 + 1.0f); // a zone shorter than a pixel still has to be visible
                ImVec2 barMin = ImVec2(x0, trackMin.y + 1.0f);
                ImVec2 barMax = ImVec2(x1, trackMax.y - 1.0f);

                float hue = (float)((uintptr_t)bar.name * 2654435761u % 1000) / 1000.0f;
                dl->AddRectFilled(barMin, barMax, (ImU32)ImColor::HSV(hue, 0.55f, 0.8f));

                char label[128];
                snprintf(label, sizeof(label), "%s  %.3f", bar.name, bar.length);
                float textRoom = x1 - x0 - 6.0f;
                const char* text = nullptr;
                if (ImGui::CalcTextSize(label).x <= textRoom)
                    text = label;
                else if (ImGui::CalcTextSize(bar.name).x <= textRoom)
                    text = bar.name;
                if (text != nullptr)
                {
                    dl->PushClipRect(barMin, barMax, true);
                    dl->AddText(ImVec2(x0 + 3.0f, trackMin.y + 3.0f), IM_COL32(16, 16, 16, 255), text);
                    dl->PopClipRect();
                }

                if (ImGui::IsMouseHoveringRect(barMin, barMax))
                {
                    ImGui::SetTooltip("%s (%s)\ncur %.3f ms | avg %.3f ms | min %.3f ms | max %.3f ms\nstart %.3f ms | end %.3f ms",
                        bar.name, queueNames[q], bar.cur, bar.avg, bar.mn, bar.mx, bar.start, bar.start + bar.length);
                    dl->AddRect(barMin, barMax, IM_COL32(255, 255, 255, 255));
                }
            }

            dl->AddRect(trackMin, trackMax, IM_COL32(90, 90, 90, 255));

            char total[32];
            snprintf(total, sizeof(total), "%.2f ms", queueTotal);
            ImVec2 totalSize = ImGui::CalcTextSize(total);
            dl->AddText(ImVec2(trackMax.x - totalSize.x - 4.0f, trackMin.y + 3.0f), IM_COL32(210, 210, 210, 255), total);

            ImGui::Dummy(ImVec2(labelWidth + width, laneHeight + lanePadding));
        }
    }

    void OpenTracy()
    {
        // additional information
        STARTUPINFO si;
        PROCESS_INFORMATION pi;

        // set the size of the structures
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        WCHAR tracycmd[] = L" -a 127.0.0.1";

        CreateProcess(L"..\\Third\\tracy-master\\profiler\\build\\Release\\tracy-profiler.exe",   // the path
            tracycmd,        // Command line
            nullptr,           // Process handle not inheritable
            nullptr,           // Thread handle not inheritable
            false,          // Set handle inheritance to FALSE
            0,              // No creation flags
            nullptr,           // Use parent's environment block
            nullptr,           // Use parent's starting directory
            &si,            // Pointer to STARTUPINFO structure
            &pi             // Pointer to PROCESS_INFORMATION structure (removed extra parentheses)
        );
        /*
        // Close process and thread handles.
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        */
    }

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

        // gathered before the header : the header shows the span the lanes below are scaled to
        std::vector<Bar> lanes[Profiler::PerQueueData::queueCount];
        double span = GatherBars(lanes);

        if (ImGui::Button("Open Tracy"))
            OpenTracy();
        ImGui::SameLine();
        ImGui::RadioButton("cur", &statMode, Stat_Cur); ImGui::SameLine();
        ImGui::RadioButton("avg", &statMode, Stat_Avg); ImGui::SameLine();
        ImGui::RadioButton("min", &statMode, Stat_Min); ImGui::SameLine();
        ImGui::RadioButton("max", &statMode, Stat_Max); ImGui::SameLine();
        ImGui::Text("| cpu %.2f ms (%.0f fps)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate); ImGui::SameLine();
        ImGui::Text("| gpu %.2f ms", span); ImGui::SameLine();
        ImGui::Text("| instances %u | meshlets %u", Profiler::instance->frameData.instancesCount, Profiler::instance->frameData.meshletsCount);

        ImGui::Separator();

        DrawLanes(lanes, span);

        ImGui::End();
    }
};
ProfilerWindow profilerWindow;

class TerrainWindow : public EditorWindow
{
    struct ChurnStat
    {
        static const uint FilterSize = 64;
        uint samples[FilterSize] = {};
        uint pushed = 0;
        void Push(uint v) { samples[pushed % FilterSize] = v; pushed++; }
        void Stats(uint& mn, uint& mx, float& avg) const
        {
            uint n = pushed < FilterSize ? pushed : FilterSize;
            mn = ~0u; mx = 0; uint sum = 0;
            for (uint i = 0; i < n; i++)
            {
                mn = samples[i] < mn ? samples[i] : mn;
                mx = samples[i] > mx ? samples[i] : mx;
                sum += samples[i];
            }
            avg = n > 0 ? (float)sum / n : 0.0f;
            if (n == 0) mn = 0;
        }
    };
    ChurnStat churnStats[4];

public:
    TerrainWindow() : EditorWindow("Terrain") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("Terrain", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Quadtree node churn");
        const char* churnNames[4] = { "unchanged", "rewritten", "created  ", "released " };
        uint churnValues[4] = {
            Profiler::instance->frameData.terrainReusedUnchanged,
            Profiler::instance->frameData.terrainReusedRewritten,
            Profiler::instance->frameData.terrainCreated,
            Profiler::instance->frameData.terrainReleased };
        for (uint i = 0; i < 4; i++)
        {
            churnStats[i].Push(churnValues[i]);
            uint mn, mx; float avg;
            churnStats[i].Stats(mn, mx, avg);
            ImGui::Text("%s : %4u | min %4u | max %4u | avg %7.1f", churnNames[i], churnValues[i], mn, mx, avg);
        }

        ImGui::End();
    }
};
TerrainWindow terrainWindow;

class AssetLibraryWindow : public EditorWindow
{
    bool showOnlyLoaded;
public:
    AssetLibraryWindow() : EditorWindow("AssetLibrary") { showOnlyLoaded = true; }
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
        ImGui::SameLine();
        if (ImGui::Checkbox("Show Only Loaded", &showOnlyLoaded))
        {
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
            bool loaded = item.second.data != nullptr;
            if (!showOnlyLoaded || loaded)
            {
                ImGui::RadioButton("##radio", loaded);
                ImGui::SameLine();
                ImGui::Text("%ul %s", item.first, item.second.path.c_str());
            }
        }

        ImGui::End();
    }
};
AssetLibraryWindow assetLibraryWindowWindow;

void InitComponentData(World::Entity ent, const ComponentInfo& info);
void InitNewEntityComponents(World::Entity ent);

class HierarchyWindow : public EditorWindow
{
    struct TreeNode
    {
        World::Entity               entity;
        String                      name;
        TreeNode* parent;
        std::vector<TreeNode*>      childs;
        uint                        indexInParent;  // Maintaining this allows us to implement linear traversal more easily
    };

    TreeNode world;
    std::vector<TreeNode> nodes;

    int componentFilterIndex = 0;
    ImGuiSelectionBasicStorage selection;

    char filterName[256] = "";
    bool showUnname = false;

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
        int unselected_count = selection->Contains(node->entity.ToUInt()) ? 1 : 0;
        if (depth == 0 || TreeNodeGetOpen(node))
        {
            for (TreeNode* child : node->childs)
                unselected_count += TreeCloseAndUnselectChildNodes(child, selection, depth + 1);
            TreeNodeSetOpen(node, false);
        }

        // Select root node if any of its child was selected, otherwise unselect
        selection->SetItemSelected(node->entity.ToUInt(), (depth == 0 && unselected_count > 0));
        return unselected_count;
    }

    void DrawNode(TreeNode* node, ImGuiSelectionBasicStorage* selection)
    {
        ImGuiTreeNodeFlags tree_node_flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        tree_node_flags |= ImGuiTreeNodeFlags_NavLeftJumpsBackHere; // Enable pressing left to jump to parent
        if (node->childs.size() == 0)
            tree_node_flags |= ImGuiTreeNodeFlags_Leaf;
        if (selection->Contains(node->entity.ToUInt()))
            tree_node_flags |= ImGuiTreeNodeFlags_Selected;
        if (node == &world)
            tree_node_flags |= ImGuiTreeNodeFlags_DefaultOpen;

        bool usingFilter = strlen(filterName) > 0;
        bool filtered = true;
        if (usingFilter)
        {
            filtered = node->name.ToLower().find(filterName) != std::string::npos;
        }
        //else
        {
            // Using SetNextItemStorageID() to specify storage id, so we can easily peek into
            // the storage holding open/close stage, using our TreeNodeGetOpen/TreeNodeSetOpen() functions.
            ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)(intptr_t)node);
            ImGui::SetNextItemStorageID(node->entity.ToUInt());
            bool isOpen = false;
            if (filtered)
                isOpen = ImGui::TreeNodeEx(node->name.c_str(), tree_node_flags);
            if (isOpen || (usingFilter && node->childs.size() > 0))
            {
                ImGui::Indent(10);
                for (TreeNode* child : node->childs)
                    DrawNode(child, selection);
                if (isOpen && filtered)
                    ImGui::TreePop();
                ImGui::Unindent(10);
            }
            else if (ImGui::IsItemToggledOpen())
            {
                TreeCloseAndUnselectChildNodes(node, selection);
            }
        }
    }

    void TreeSetAllInOpenNodes(TreeNode* node, ImGuiSelectionBasicStorage* selection, bool selected)
    {
        if (node->parent != NULL) // Root node isn't visible nor selectable in our scheme
            selection->SetItemSelected(node->entity.ToUInt(), selected);
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
                    selection->SetItemSelected(node->entity.ToUInt(), req.Selected);
            }
        }
    }

public:
    HierarchyWindow() : EditorWindow("Hierarchy") {}
    void Update() override final
    {
        ZoneScoped;

        if (editorState.dirtyHierarchy || World::instance->structureChanged)
        {
            nodes.clear();

            world.entity = entityInvalid;
            world.name = "world";
            world.parent = NULL;
            world.childs.clear();
            world.indexInParent = 0;

            Components::Mask mask = Components::Entity::mask;
            if (knownComponents.size() > 0)
                mask = knownComponents[componentFilterIndex].mask;
            uint instanceQueryIndex = World::instance->Query(mask, 0, false);
            auto& result = World::instance->frameQueries[instanceQueryIndex];

            for (uint i = 0; i < result.size(); i++)
            {
                TreeNode newNode;
                newNode.entity = { result[i].Get<Components::Entity>() };
                bool hasName = result[i].Has<Components::Name>();
                if (hasName)
                {
                    newNode.name = std::format("{}{}", result[i].Get<Components::Name>().name, i);
                }
                else
                {
                    newNode.name = std::format("{}{}", "un-name", i);
                }
                newNode.parent = &world;
                if (showUnname || hasName)
                    nodes.push_back(newNode);
            }

            for (uint i = 0; i < nodes.size(); i++)
            {
                TreeNode& node = nodes[i];

                // Use the node's OWN entity, not result[i]: nodes is filtered (unnamed entities
                // are skipped when !showUnname), so result[i] can be a different entity than node.
                World::Entity nodeEnt = node.entity;
                bool hasParent = nodeEnt.Has<Components::Parent>();
                if (!hasParent)
                {
                    node.parent = &world;
                }
                else
                {
                    auto& pent = nodeEnt.Get<Components::Parent>().entity;
                    if (!pent.IsValid())// || !World::Entity(pent.Get().index).Has<Components::Instance>())
                    {
                        node.parent = &world;
                    }
                    else
                    {
                        World::Entity parent = { pent.Get() };
                        for (uint j = 0; j < nodes.size(); j++)
                        {
                            TreeNode& nodeParent = nodes[j];
                            if (nodeParent.entity == parent)
                            {
                                node.parent = &nodeParent;
                            }
                        }
                    }
                }

                node.indexInParent = (uint)node.parent->childs.size();
                node.parent->childs.push_back(&node);
            }
            editorState.dirtyHierarchy = false;
        }

        if (!ImGui::Begin("HierarchyWindow", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }
        editorState.dirtyHierarchy |= ImGui::Combo("##Filter", &componentFilterIndex, [](void* data, int n) { return (*(std::vector<ComponentInfo>*)data)[n].name.c_str(); }, (void*)&knownComponents, (int)knownComponents.size());
        ImGui::SameLine();
        if (ImGui::Button("new Entity"))
        {
            World::Entity ent;
            ent.Make(knownComponents[componentFilterIndex].mask);
            InitNewEntityComponents(ent);
            editorState.dirtyHierarchy |= true;
        }

        ImGui::InputText("filter", filterName, 256);
        ImGui::Checkbox("show un-names", &showUnname);

        {
            //ImGuiSelectionBasicStorage previousSelection = selection;
            if (editorState.selectedObject == entityInvalid)
                selection.Clear();

            void* it = NULL;
            ImGuiID id = 0;
            selection.GetNextSelectedItem(&it, &id);
            if (id != editorState.selectedObject.ToUInt())
            {
                selection.Clear();
            }

            // The tree's left gutter is pure style: the theme sets FramePadding.x to 20 (Moonlight,
            // SetupImGuiStyle in Renderer.h), and a tree node reserves FontSize + 2*FramePadding.x
            // before its label for the collapsing arrow (TreeNodeBehavior) -- ~54px of empty space
            // on EVERY row, on top of the 20px the FrameStyle child uses as its own padding. Both
            // read from FramePadding.x, so one push shrinks them together, down to the arrow itself.
            // Kept local to the tree: the 20px is what gives the filter/combo widgets above their
            // pill shape, and those are drawn outside this scope.
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, ImGui::GetStyle().FramePadding.y));
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
                    editorState.selectedObject.FromUInt(id);
                }

                ImGui::EndChild();
            }
            ImGui::PopStyleVar();
        }

        //always add selected object to selection storage
        if (editorState.selectedObject != entityInvalid)
            selection.SetItemSelected(editorState.selectedObject.ToUInt(), true);

        if ((ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete)) || IOs::instance->keys.pressed[VK_DELETE])
        {
            void* it = NULL;
            ImGuiID id = 0;
            selection.GetNextSelectedItem(&it, &id);
            while (id != 0)
            {
                editorState.selectedObject.FromUInt(id);
                editorState.selectedObject.Release();
                editorState.selectedObject = entityInvalid;
                if (!selection.GetNextSelectedItem(&it, &id))
                    break;
            }
            editorState.dirtyHierarchy = true;
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
private:
    // Preview resource used to hold a single Z slice of a 3D texture for ImGui display
    Resource slicePreviewResource;
    uint slicePreviewWidth = 0;
    uint slicePreviewHeight = 0;
    DXGI_FORMAT slicePreviewFormat = DXGI_FORMAT_UNKNOWN;

    // Ensure preview texture exists and matches dimensions/format
    void EnsureSlicePreview(uint width, uint height, DXGI_FORMAT format)
    {
        if (slicePreviewResource.GetResource() == nullptr ||
            slicePreviewWidth != width ||
            slicePreviewHeight != height ||
            slicePreviewFormat != format)
        {
            slicePreviewResource.Release(false);
            slicePreviewWidth = width;
            slicePreviewHeight = height;
            slicePreviewFormat = format;
            // create single-mip 2D texture sized to target mip level
            slicePreviewResource.CreateTexture(uint3(width, height, 1), format, false, "SlicePreview");
        }
    }

    // Copy a single Z slice (at given mip) from a 3D texture into the slicePreviewResource (which is a 2D texture).
    // Synchronous helper using the copy queue; waits for completion before returning.
    void Copy3DSliceToPreview(ID3D12Resource* srcResource, const D3D12_RESOURCE_DESC& srcDesc, int mip, int slice)
    {
        if (srcResource == nullptr)
            return;

        // compute mip dimensions
        uint mipW = max(1u, (uint)(srcDesc.Width >> mip));
        uint mipH = max(1u, srcDesc.Height >> mip);

        // ensure preview resource matches
        EnsureSlicePreview(mipW, mipH, srcDesc.Format);
        ID3D12Resource* dstResource = slicePreviewResource.GetResource();
        if (dstResource == nullptr)
            return;

        HRESULT hr = S_OK;

        // Create temporary command allocator + list on COPY queue
        ID3D12CommandAllocator* alloc = nullptr;
        ID3D12GraphicsCommandList* cmd = nullptr;
        hr = GPU::instance->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&alloc));
        if (FAILED(hr)) { GPU::PrintDeviceRemovedReason(hr); return; }
        hr = GPU::instance->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, alloc, nullptr, IID_PPV_ARGS(&cmd));
        if (FAILED(hr)) { GPU::PrintDeviceRemovedReason(hr); alloc->Release(); return; }

        // Transition src -> COPY_SOURCE and dst -> COPY_DEST
        {
            D3D12_RESOURCE_BARRIER barriers[2] = {};
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition.pResource = srcResource;
            barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

            barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[1].Transition.pResource = dstResource;
            barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

            //cmd->ResourceBarrier(2, barriers);
        }

        // Setup copy locations
        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = srcResource;
        // For 3D textures the subresource index for a given mip is just the mip index
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = mip; // arraySlice = 0, planeSlice = 0

        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = dstResource;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0; // first (and only) subresource in preview texture

        // Define source box to select single Z slice: front = slice, back = slice+1
        D3D12_BOX srcBox = {};
        srcBox.left = 0;
        srcBox.top = 0;
        srcBox.right = mipW;
        srcBox.bottom = mipH;
        srcBox.front = slice;
        srcBox.back = slice + 1;

        // Perform copy
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);

        // Transition resources back to COMMON
        {
            D3D12_RESOURCE_BARRIER barriers[2] = {};
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition.pResource = srcResource;
            barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;

            barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[1].Transition.pResource = dstResource;
            barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;

            cmd->ResourceBarrier(2, barriers);
        }

        hr = cmd->Close();
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            cmd->Release();
            alloc->Release();
            return;
        }

        Renderer::instance->ExecuteImmediate(cmd, GPU::instance->copyQueue);

        // cleanup
        cmd->Release();
        alloc->Release();
    }

public:
    void Update() override final
    {
        ZoneScoped;

        if (!ImGui::Begin("GPUResources", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("##GPUResourcesTabs"))
        {
            if (ImGui::BeginTabItem("Resources"))
            {
                DrawResourcesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Upload Pool"))
            {
                DrawUploadPoolStats();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void DrawResourcesTab()
    {
        const char* names[] = { "Resources", "Content" };
        static Resource selectedResource = {};
        static int mip = 0;
        static int slice = 0;
        bool selected;

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::BeginGroup();
        const ImGuiWindowFlags child_flags = ImGuiWindowFlags_MenuBar;
        ImGuiID child_id = ImGui::GetID((void*)(intptr_t)0);
        const bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(300, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders, child_flags);
        {
            const std::lock_guard<std::mutex> lock(Resource::lock);

            auto resBytes = [](const Resource& r) -> UINT64
                {
                    return r.allocation ? r.allocation->GetSize() : 0;
                };

            // Tally VRAM per category for the headers and the grand total.
            UINT64 catTotal[(int)ResourceCategory::Count] = {};
            UINT64 grandTotal = 0;
            for (int i = 0; i < Resource::allResources.size(); i++)
            {
                UINT64 b = resBytes(Resource::allResources[i]);
                catTotal[(int)Resource::allResources[i].category] += b;
                grandTotal += b;
            }

            const double toMB = 1.0 / (1024.0 * 1024.0);
            ImGui::Text("Tracked VRAM: %.1f MB", grandTotal * toMB);
            ImGui::Separator();

            for (int c = 0; c < (int)ResourceCategory::Count; c++)
            {
                if (catTotal[c] == 0)
                {
                    // still skip if there genuinely are no resources in this category
                    bool any = false;
                    for (int i = 0; i < Resource::allResources.size(); i++)
                        if ((int)Resource::allResources[i].category == c) { any = true; break; }
                    if (!any)
                        continue;
                }

                char header[160];
                snprintf(header, sizeof(header), "%s  -  %.1f MB###cat%d",
                    ResourceCategoryName((ResourceCategory)c), catTotal[c] * toMB, c);

                if (ImGui::TreeNode(header))
                {
                    for (int i = 0; i < Resource::allResources.size(); i++)
                    {
                        if ((int)Resource::allResources[i].category != c)
                            continue;

                        ImGui::PushID(i);
                        const char* dispName = Resource::allResourcesNames[i].empty() ? "(unnamed)" : Resource::allResourcesNames[i].c_str();
                        selected = selectedResource.allocation != nullptr && selectedResource.allocation == Resource::allResources[i].allocation;
                        if (selected)
                        {
                            ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", dispName);
                        }
                        else
                        {
                            ImGui::Selectable(dispName, &selected);
                            if (selected)
                                selectedResource = Resource::allResources[i];
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("%.2f MB", resBytes(Resource::allResources[i]) * toMB);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
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

            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                // Buffers have no image preview; show allocation info instead of
                // trying to build a (mip-based) texture view, which used to crash.
                UINT64 sizeBytes = selectedResource.allocation ? selectedResource.allocation->GetSize() : (UINT64)desc.Width;
                ImGui::Text("Buffer");
                ImGui::Text("Category: %s", ResourceCategoryName(selectedResource.category));
                ImGui::Text("Size: %.3f MB  (%llu bytes)", sizeBytes / (1024.0 * 1024.0), (unsigned long long)sizeBytes);
                uint stride = selectedResource.stride;
                if (stride > 0)
                {
                    ImGui::Text("Stride: %u bytes", stride);
                    ImGui::Text("Elements: %llu", (unsigned long long)(desc.Width / stride));
                }
            }
            else
            {
                float textureW = (float)desc.Width;
                float textureH = (float)desc.Height;
                float ratio = textureH > 0 ? textureW / textureH : 1.0f;
                ImVec2 img_sz = ImGui::GetContentRegionAvail();
                img_sz.y = img_sz.x / ratio;

                // mip/slice persist across selections, clamp to the current resource.
                if (mip < 0) mip = 0;
                if (mip > (int)desc.MipLevels - 1) mip = (int)desc.MipLevels - 1;
                if (slice < 0) slice = 0;
                if (slice > (int)desc.DepthOrArraySize - 1) slice = (int)desc.DepthOrArraySize - 1;

                ImGui::Text("%.0fx%.0f", textureW, textureH);
                if (desc.MipLevels > 1)
                {
                    ImGui::SameLine();
                    ImGui::SliderInt("mip", &mip, 0, desc.MipLevels - 1);
                }
                if (desc.DepthOrArraySize > 1)
                {
                    ImGui::SameLine();
                    ImGui::SliderInt("slice", &slice, 0, desc.DepthOrArraySize - 1);
                }

                // Simple display controls: isolate colour channels via the image tint.
                static bool showR = true, showG = true, showB = true;
                ImGui::TextUnformatted("Channels:");
                ImGui::SameLine(); ImGui::Checkbox("R", &showR);
                ImGui::SameLine(); ImGui::Checkbox("G", &showG);
                ImGui::SameLine(); ImGui::Checkbox("B", &showB);

                // Create texture view
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
                ZeroMemory(&srvDesc, sizeof(srvDesc));

                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Format = desc.Format;

                if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                {
                    // Copy requested slice into a 2D preview texture and create a 2D SRV from it for ImGui
                    // perform copy (synchronous)
                    Copy3DSliceToPreview(selectedResource.GetResource(), desc, mip, slice);

                    // Create SRV for preview 2D resource
                    ID3D12Resource* viewRes = slicePreviewResource.GetResource();
                    if (viewRes)
                    {
                        D3D12_RESOURCE_DESC vdesc = viewRes->GetDesc();
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srvDesc.Texture2D.MostDetailedMip = 0;
                        srvDesc.Texture2D.MipLevels = 1;
                        srvDesc.Texture2D.PlaneSlice = 0;
                        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

                        GPU::instance->device->CreateShaderResourceView(viewRes, &srvDesc, UI::instance->imgCPUHandle);
                    }
                }
                else // TEXTURE2D / TEXTURE1D
                {
                    if (srvDesc.Format == DXGI_FORMAT_D32_FLOAT) srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MostDetailedMip = mip;
                    srvDesc.Texture2D.MipLevels = -1;
                    srvDesc.Texture2D.PlaneSlice = 0;
                    srvDesc.Texture2D.ResourceMinLODClamp = 0;
                    GPU::instance->device->CreateShaderResourceView(selectedResource.GetResource(), &srvDesc, UI::instance->imgCPUHandle);
                }

                ImTextureID my_tex_id = UI::instance->imgGPUHandle.ptr;

                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 uv_min = ImVec2(0.0f, 0.0f);                 // Top-left
                ImVec2 uv_max = ImVec2(1.0f, 1.0f);                 // Lower-right
                ImVec4 tint_col = ImVec4(showR ? 1.0f : 0.0f, showG ? 1.0f : 0.0f, showB ? 1.0f : 0.0f, 1.0f);
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
    }

    // Stats for the reusable upload-buffer pool (see UploadBufferPool in GPU.h): per power-of-two
    // bucket, how many buffers are sitting free vs. in-flight (recorded but not yet recycled), and
    // the VRAM the pool is holding onto.
    void DrawUploadPoolStats()
    {
        UploadBufferPool* pool = GPU::instance ? &GPU::instance->uploadBufferPool : nullptr;
        if (pool == nullptr)
        {
            ImGui::TextUnformatted("Upload buffer pool not initialized.");
            return;
        }

        const double toMB = 1.0 / (1024.0 * 1024.0);

        // Snapshot counts under the pool lock so we don't read the vectors while a loading thread
        // mutates them.
        uint freeCount[UploadBufferPool::NUM_BUCKETS] = {};
        uint pendingCount[UploadBufferPool::NUM_BUCKETS] = {};
        uint pendingTotal = 0;
        {
            const std::lock_guard<std::mutex> lock(pool->poolLock);
            for (uint b = 0; b < UploadBufferPool::NUM_BUCKETS; b++)
                freeCount[b] = (uint)pool->freeLists[b].size();
            for (auto& p : pool->pending)
            {
                if (p.bucket >= 0 && p.bucket < (int)UploadBufferPool::NUM_BUCKETS)
                    pendingCount[p.bucket]++;
                pendingTotal++;
            }
        }

        UINT64 totalBytes = 0;
        uint totalBuffers = 0;
        for (uint b = 0; b < UploadBufferPool::NUM_BUCKETS; b++)
        {
            UINT64 cap = 1ull << (UploadBufferPool::MIN_SHIFT + b);
            totalBytes += cap * (freeCount[b] + pendingCount[b]);
            totalBuffers += freeCount[b] + pendingCount[b];
        }

        ImGui::Text("Pooled VRAM: %.2f MB    Buffers: %u    In-flight: %u",
            totalBytes * toMB, totalBuffers, pendingTotal);
        ImGui::Separator();

        if (ImGui::BeginTable("uploadPoolTable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_HighlightHoveredColumn))
        {
            ImGui::TableSetupColumn("Bucket");
            ImGui::TableSetupColumn("Free");
            ImGui::TableSetupColumn("In-flight");
            ImGui::TableSetupColumn("Total");
            ImGui::TableSetupColumn("VRAM");
            ImGui::TableHeadersRow();

            for (uint b = 0; b < UploadBufferPool::NUM_BUCKETS; b++)
            {
                UINT64 cap = 1ull << (UploadBufferPool::MIN_SHIFT + b);
                uint total = freeCount[b] + pendingCount[b];

                ImGui::TableNextRow();
                uint col = 0;
                ImGui::TableSetColumnIndex(col++);
                if (cap < (1u << 20))
                    ImGui::Text("%llu KB", (unsigned long long)(cap >> 10));
                else
                    ImGui::Text("%llu MB", (unsigned long long)(cap >> 20));
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", freeCount[b]);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", pendingCount[b]);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", total);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%.2f MB", cap * total * toMB);
            }
            ImGui::EndTable();
        }
    }
};
GPUResourcesWindow gpuResourcesWindow;


class InstancesWindow : public EditorWindow
{
public:
    InstancesWindow() : EditorWindow("Instances") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("InstancesWindow", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        float width = ImGui::GetContentRegionAvail().x * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f;

        if (ImGui::BeginTable("cpuTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_HighlightHoveredColumn, ImVec2(width, 0)))
        {
            ImGui::TableHeader("CPU Data");
            ImGui::TableSetupColumn("meshIndex");
            ImGui::TableSetupColumn("materialIndex");
            ImGui::TableSetupColumn("objectID");
            ImGui::TableSetupColumn("rayTracingBLAS");
            ImGui::TableSetupColumn("current");
            ImGui::TableSetupColumn("previous");
            ImGui::TableHeadersRow();
            for (auto& item : Renderer::instance->mainView.viewWorld.instances.cpuData)
            {
                ImGui::TableNextRow();
                uint col = 0;
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", item.meshIndex);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", item.materialIndex);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", item.objectID);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", item.rayTracingBLAS);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%4.1f %4.1f %4.1f", item.current[3][0], item.current[3][1], item.current[3][2]);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%4.1f %4.1f %4.1f", item.previous[3][0], item.previous[3][1], item.previous[3][2]);
            }
            ImGui::EndTable();
        }
        ImGui::SameLine();
        if (ImGui::BeginTable("readbackTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_HighlightHoveredColumn, ImVec2(width, 0)))
        {
            ImGui::TableHeader("GPU Readback");
            ImGui::TableSetupColumn("meshIndex");
            ImGui::TableSetupColumn("materialIndex");
            ImGui::TableSetupColumn("objectID");
            ImGui::TableSetupColumn("rayTracingBLAS");
            ImGui::TableSetupColumn("current");
            ImGui::TableSetupColumn("previous");
            ImGui::TableHeadersRow();
            for (auto& item : Renderer::instance->mainView.viewWorld.instancesReadBackDebug)
            {
                ImGui::TableNextRow();
                uint col = 0;
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", item.meshIndex);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", item.materialIndex);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", item.objectID);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%u", item.rayTracingBLAS);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%4.1f %4.1f %4.1f", item.current[3][0], item.current[3][1], item.current[3][2]);
                ImGui::TableSetColumnIndex(col++);
                ImGui::Text("%4.1f %4.1f %4.1f", item.previous[3][0], item.previous[3][1], item.previous[3][2]);
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }
};
InstancesWindow instancesWindow;

#include "Shaders/structs.hlsl"
// Single window combining frame/debug options with post-process tuning.
// Rarely-touched parameters live under "Advanced" tree nodes so the common
// controls stay front and center.
class RenderingWindow : public EditorWindow
{
public:
    RenderingWindow() : EditorWindow("Rendering") { isOpen = true; }
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("Rendering", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        auto& rt = Renderer::instance->mainView.raytracingContext.rtParameters;

        if (ImGui::CollapsingHeader("Debug View"))
        {
            int debugModeIndex = (int)options.debugMode;
            const char* items[] = { "none", "ray", "boundingSphere" };
            ImGui::Combo("debugMode", &debugModeIndex, items, IM_ARRAYSIZE(items));
            ImGui::SetItemTooltip("Overlay debug geometry (rays / bounding spheres).");
            options.debugMode = (Options::DebugMode)debugModeIndex;

            int debugDrawIndex = (int)options.debugDraw;
            const char* itemsDraw[] = { "none", "albedo", "normals", "clusters", "triangles", "lighting", "GIprobes", "GIBounces", "GIAlbedo", "GINormals", "overdraw", "hazeMap" };
            ImGui::Combo("debugDraw", &debugDrawIndex, itemsDraw, IM_ARRAYSIZE(itemsDraw));
            ImGui::SetItemTooltip("Replace the final image with a debug buffer visualization.");
            options.debugDraw = (Options::DebugDraw)debugDrawIndex;
            if (options.debugDraw == Options::DebugDraw::hazeMap)
            {
                ImGui::SliderInt("haze layer", &options.debugHazeLayer, 1, HLSL::HazeLayerCount - 1);
                ImGui::SetItemTooltip("Haze layer whose map is shown. Magenta: that layer has no maps (no HazeLayer component with this index).");
            }

            ImGui::Checkbox("stopFrustumUpdate", &options.stopFrustumUpdate);
            ImGui::SetItemTooltip("Freeze the culling frustum to inspect culling from a fixed viewpoint.");
            ImGui::Checkbox("stopBufferUpload", &options.stopBufferUpload);
            ImGui::SetItemTooltip("Stop uploading buffers to the GPU (freeze scene data).");
            ImGui::Checkbox("stepMotion", &options.stepMotion);
            ImGui::SetItemTooltip("Advance motion one frame at a time.");
            ImGui::Checkbox("shaderHotReload", &options.shaderReload);
            ImGui::SetItemTooltip("Reload shaders from disk when they change.");
            ImGui::Checkbox("enableStructuredCommandBuffersReadback", &options.enableStructuredCommandBuffersReadback);
            ImGui::SetItemTooltip("Read back structured command buffers from the GPU for inspection.");
            ImGui::Checkbox("drawPhysicsShapes", &options.drawPhysicsShapes);
            ImGui::SetItemTooltip("Wireframe every physics collider, coloured by motion type (static / kinematic / dynamic).\nThe SELECTED entity's collider is drawn even when this is off.");
            ImGui::Checkbox("drawPhysicsConstraints", &options.drawPhysicsConstraints);
            ImGui::SetItemTooltip("Draw every physics constraint: anchors, axes, limits, and a yellow line joining the two bodies.\nWorld-anchored constraints show a yellow marker at the pinned point instead.\nConstraints touching the SELECTED entity are drawn even when this is off, whether it owns the Constraint or is its target.");
            ImGui::Checkbox("followVehicle", &options.followVehicle);
            ImGui::SetItemTooltip("While playing, park the camera behind the car Systems::Driver is steering, on a critically\ndamped spring. Off gives the free-fly camera back. The free-fly WASD keys do nothing while this\nis following -- press R to set the car back on its wheels.");
            if (options.followVehicle)
            {
                ImGui::DragFloat("followDistance", &options.followDistance, 0.05f, 0.5f, 20.0f);
                ImGui::SetItemTooltip("How far behind the car the camera sits, in multiples of the chassis half length.");
                ImGui::DragFloat("followHeight", &options.followHeight, 0.05f, -2.0f, 20.0f);
                ImGui::SetItemTooltip("How far above the car the camera sits, in multiples of the chassis half length.");
                ImGui::DragFloat("followLookAtHeight", &options.followLookAtHeight, 0.05f, -2.0f, 20.0f);
                ImGui::SetItemTooltip("Where on the car the camera aims, above its origin, in multiples of the chassis half length.");
                ImGui::DragFloat("followSmoothTime", &options.followSmoothTime, 0.01f, 0.0f, 2.0f);
                ImGui::SetItemTooltip("Seconds the spring takes to close the gap. 0 welds the camera to the car, 0.25 is a normal\nchase lag, past ~0.6 it starts feeling like the car is towing the camera on a rope.");
            }
        }

        if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("frontToBackSort", &options.frontToBackSort);
            ImGui::SetItemTooltip("Sort instances front-to-back to reduce overdraw.");
            ImGui::BeginDisabled(!options.frontToBackSort);
            ImGui::SliderFloat("sortMaxDistance", &options.sortMaxDistance, 1.0f, 100000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Max distance considered when sorting instances.");
            ImGui::EndDisabled();
            ImGui::SliderFloat("lodDistanceMultiplier", &options.lodDistanceMultiplier, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Scales how fast meshes drop to coarser LODs with distance (higher = coarser sooner).");
            ImGui::SliderFloat("distanceCullingValue", &options.distanceCullingValue, 1.0f, 16.0f, "%.1f");
            ImGui::SetItemTooltip("Cull instances whose distance-based LOD exceeds this value (lower = cull closer to the camera).");
            ImGui::SliderFloat("distanceCullingValueRT", &options.distanceCullingValueRT, 1.0f, 16.0f, "%.1f");
            ImGui::SetItemTooltip("Same threshold for the raytracing scene: culled instances stay in the TLAS but point at a null BLAS.");
        }

        if (ImGui::CollapsingHeader("Ray Tracing", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderInt("frameFilter", (int*)&rt.maxFrameFilteringCount, 1, 512, "%d", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("spacialRadius", &rt.spacialRadius, 0, 128);
            ImGui::SliderInt("spacialSamples", (int*)&rt.spacialSampleCount, 0, 64);

            if (ImGui::TreeNode("Advanced##rt"))
            {
                ImGui::SliderFloat("randBias", &rt.reservoirRandBias, -1, 1);
                ImGui::SliderFloat("spacialRandBias", &rt.reservoirSpacialRandBias, 0, 1);
                ImGui::SliderFloat("SHARCSceneScale", &rt.SHARCSceneScale, 1, 64, "%f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("SHARCRadianceScale", &rt.SHARCRadianceScale, 0.1, 10);
                ImGui::SliderFloat("SHARCRoughnessThreshold", &rt.SHARCRoughnessThreshold, 0, 1);
                ImGui::SliderInt("SHARCSamplesPerPixel", (int*)&rt.SHARCSamplesPerPixel, 1, 8);
                ImGui::SliderInt("SHARCAccumulationFrameNum", (int*)&rt.SHARCAccumulationFrameNum, 1, 512, "%d", ImGuiSliderFlags_Logarithmic);
                ImGui::TreePop();
            }
        }

        if (ImGui::CollapsingHeader("Upscaling", ImGuiTreeNodeFlags_DefaultOpen))
        {
            MainView& view = Renderer::instance->mainView;
            DLSS& dlss = view.dlss;

            // Axis 1: technique. dlss = Super Resolution, dlssd = Ray Reconstruction (separate NGX
            // features, not models of one another). Switching here recreates the feature in Render().
            const char* modes[] = { "none", "taa", "dlss", "dlssd" };
            ImGui::Combo("technique", (int*)&view.upscaling, modes, 4);

            const bool dlssActive = (view.upscaling == HLSL::Upscaling::dlss || view.upscaling == HLSL::Upscaling::dlssd);
            if (dlssActive)
            {
                // Axis 2: quality. Shared by SR and RR; drives the internal render resolution, so a
                // change triggers a deferred view rebuild (Renderer::ApplyPendingQualityChange).
                static const char* qualityNames[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "Ultra Quality", "DLAA" };
                static const NVSDK_NGX_PerfQuality_Value qualityValues[] = {
                    NVSDK_NGX_PerfQuality_Value_UltraPerformance,
                    NVSDK_NGX_PerfQuality_Value_MaxPerf,
                    NVSDK_NGX_PerfQuality_Value_Balanced,
                    NVSDK_NGX_PerfQuality_Value_MaxQuality,
                    NVSDK_NGX_PerfQuality_Value_UltraQuality,
                    NVSDK_NGX_PerfQuality_Value_DLAA,
                };
                int qualityIdx = 2; // Balanced
                for (int i = 0; i < IM_ARRAYSIZE(qualityValues); i++)
                    if (qualityValues[i] == dlss.requestedQuality) { qualityIdx = i; break; }
                if (ImGui::Combo("quality", &qualityIdx, qualityNames, IM_ARRAYSIZE(qualityNames)))
                {
                    dlss.requestedQuality = qualityValues[qualityIdx];
                    // Only rebuild if the render resolution actually changes vs the active mode.
                    dlss.qualityChangePending = (dlss.requestedQuality != dlss.perf_quality);
                }

                // Axis 3: model / preset. SR and RR have distinct preset enums, so the list is
                // context-sensitive to the active technique. A change just recreates the feature.
                if (view.upscaling == HLSL::Upscaling::dlss)
                {
                    static const char* srNames[] = { "Default", "Transformer (K)", "Transformer (J)", "Ultra-Perf (L)", "Performance (M)" };
                    static const NVSDK_NGX_DLSS_Hint_Render_Preset srValues[] = {
                        NVSDK_NGX_DLSS_Hint_Render_Preset_Default,
                        NVSDK_NGX_DLSS_Hint_Render_Preset_K,
                        NVSDK_NGX_DLSS_Hint_Render_Preset_J,
                        NVSDK_NGX_DLSS_Hint_Render_Preset_L,
                        NVSDK_NGX_DLSS_Hint_Render_Preset_M,
                    };
                    int modelIdx = 0;
                    for (int i = 0; i < IM_ARRAYSIZE(srValues); i++)
                        if (srValues[i] == dlss.dlssPreset) { modelIdx = i; break; }
                    if (ImGui::Combo("model", &modelIdx, srNames, IM_ARRAYSIZE(srNames)))
                    {
                        dlss.dlssPreset = srValues[modelIdx];
                        dlss.featureDirty = true;
                    }
                }
                else // dlssd: Ray Reconstruction
                {
                    static const char* rrNames[] = { "Default", "Transformer (D)", "Latest Transformer (E)" };
                    static const NVSDK_NGX_RayReconstruction_Hint_Render_Preset rrValues[] = {
                        NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default,
                        NVSDK_NGX_RayReconstruction_Hint_Render_Preset_D,
                        NVSDK_NGX_RayReconstruction_Hint_Render_Preset_E,
                    };
                    int modelIdx = 0;
                    for (int i = 0; i < IM_ARRAYSIZE(rrValues); i++)
                        if (rrValues[i] == dlss.dlssdPreset) { modelIdx = i; break; }
                    if (ImGui::Combo("model", &modelIdx, rrNames, IM_ARRAYSIZE(rrNames)))
                    {
                        dlss.dlssdPreset = rrValues[modelIdx];
                        dlss.featureDirty = true;
                    }
                }
            }
        }

        ImGui::End();
    }
};
RenderingWindow renderingWindow;

class MeshStorageWindow : public EditorWindow
{
    void DrawASTable(MeshStorage* ms, double toMB)
    {
        ImGui::SeparatorText("Acceleration structures");

        struct Row
        {
            std::string name;
            const char* kind;
            uint primitives;
            uint scratchBytes;
            uint scratchEnd;
            uint resultBytes;
            uint builds;
            uint lastFrame;
            uint meshIndex;
            bool isOverride;
        };
        std::vector<Row> rows;
        {
            std::lock_guard<std::mutex> lk(ms->asBuildsLock);
            rows.reserve(ms->asBuilds.size());
            for (auto& kv : ms->asBuilds)
            {
                auto& b = kv.second;
                Row r;
                r.kind = MeshStorage::ASKindName(b.kind);
                r.primitives = b.primitives;
                r.scratchBytes = b.scratchBytes;
                r.scratchEnd = b.scratchEnd;
                r.resultBytes = b.resultBytes;
                r.builds = b.builds;
                r.lastFrame = b.lastFrame;
                r.meshIndex = b.index;
                r.isOverride = false;
                r.name = b.kind == MeshStorage::AS_TLAS ? std::format("view {}", b.index) : std::format("mesh {}", b.index);
                if (b.kind != MeshStorage::AS_TLAS)
                {
                    // a terrain node's BLAS belongs to an override mesh : follow it back to the
                    // source asset, which is the name that means something to the reader
                    auto src = ms->overrideSourceByIndex.find(b.index);
                    if (src != ms->overrideSourceByIndex.end())
                    {
                        r.meshIndex = src->second;
                        r.isOverride = true;
                    }
                }
                rows.push_back(std::move(r));
            }
        }

        if (AssetLibrary::instance != nullptr)
        {
            std::unordered_map<uint, std::string> namesByStorageIndex;
            for (auto& kv : AssetLibrary::instance->map)
            {
                if (kv.second.type != AssetLibrary::AssetType::mesh || kv.second.storageIndex == ~0u)
                    continue;
                const String& path = kv.second.originalFilePath.size() > 0 ? kv.second.originalFilePath : kv.second.path;
                std::string name = path.c_str();
                size_t slash = name.find_last_of("/\\");
                namesByStorageIndex[kv.second.storageIndex] = slash == std::string::npos ? name : name.substr(slash + 1);
            }
            for (auto& r : rows)
            {
                if (r.kind == MeshStorage::ASKindName(MeshStorage::AS_TLAS))
                    continue;
                auto it = namesByStorageIndex.find(r.meshIndex);
                if (it != namesByStorageIndex.end())
                    r.name = r.isOverride ? it->second + " (terrain node)" : it->second;
                else if (r.isOverride)
                    r.name += " (terrain node)";
            }
        }

        if (rows.empty())
        {
            ImGui::TextDisabled("No acceleration structure built yet.");
            return;
        }

        uint resultTotal = 0, batchEndMax = 0;
        for (auto& r : rows) { resultTotal += r.resultBytes; batchEndMax = max(batchEndMax, r.scratchEnd); }
        ImGui::Text("%zu structures | %.2f MB live", rows.size(), resultTotal * toMB);
        ImGui::Text("largest batch end %.2f MB -> what the %.0f MB reservation must cover",
            batchEndMax * toMB, ms->maxScratchSizeInBytes * toMB);

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_HighlightHoveredColumn;
        if (ImGui::BeginTable("##ASBuilds", 7, flags, ImVec2(0, 220)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("asset", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("primitives", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("scratch MB", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 85);
            ImGui::TableSetupColumn("batch end MB", ImGuiTableColumnFlags_WidthFixed, 95);
            ImGui::TableSetupColumn("size MB", ImGuiTableColumnFlags_WidthFixed, 75);
            ImGui::TableSetupColumn("builds", ImGuiTableColumnFlags_WidthFixed, 55);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs())
            {
                if (sort->SpecsCount > 0)
                {
                    const ImGuiTableColumnSortSpecs& spec = sort->Specs[0];
                    bool asc = spec.SortDirection == ImGuiSortDirection_Ascending;
                    std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b)
                        {
                            int cmp = 0;
                            switch (spec.ColumnIndex)
                            {
                            case 0: cmp = a.name.compare(b.name); break;
                            case 1: cmp = strcmp(a.kind, b.kind); break;
                            case 2: cmp = a.primitives < b.primitives ? -1 : (a.primitives > b.primitives ? 1 : 0); break;
                            case 3: cmp = a.scratchBytes < b.scratchBytes ? -1 : (a.scratchBytes > b.scratchBytes ? 1 : 0); break;
                            case 4: cmp = a.scratchEnd < b.scratchEnd ? -1 : (a.scratchEnd > b.scratchEnd ? 1 : 0); break;
                            case 5: cmp = a.resultBytes < b.resultBytes ? -1 : (a.resultBytes > b.resultBytes ? 1 : 0); break;
                            default: cmp = a.builds < b.builds ? -1 : (a.builds > b.builds ? 1 : 0); break;
                            }
                            return asc ? cmp < 0 : cmp > 0;
                        });
                }
            }

            uint frameNumber = GPU::instance != nullptr ? GPU::instance->frameNumber : 0;
            for (auto& r : rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // built this frame -> highlighted, so a mesh rebuilding every frame stands out
                bool rebuiltNow = frameNumber > 0 && r.lastFrame + 2 >= frameNumber;
                if (rebuiltNow)
                    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", r.name.c_str());
                else
                    ImGui::TextUnformatted(r.name.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("last built on frame %u (%u builds)", r.lastFrame, r.builds);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(r.kind);
                ImGui::TableNextColumn(); ImGui::Text("%u", r.primitives);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", r.scratchBytes * toMB);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", r.scratchEnd * toMB);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", r.resultBytes * toMB);
                ImGui::TableNextColumn(); ImGui::Text("%u", r.builds);
            }
            ImGui::EndTable();
        }
    }

public:
    MeshStorageWindow() : EditorWindow("MeshStorage") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("MeshStorageWindow", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        MeshStorage* ms = MeshStorage::instance;
        if (ms == nullptr)
        {
            ImGui::TextDisabled("Mesh storage not initialized.");
            ImGui::End();
            return;
        }

        const std::lock_guard<std::recursive_mutex> lock(ms->lock);

        const double toMB = 1.0 / (1024.0 * 1024.0);
        auto bytes = [](uint count, size_t stride) { return (double)count * (double)stride; };

        // Per-buffer occupancy (used / capacity).
        auto bar = [&](const char* label, uint used, uint capacity, size_t stride)
            {
                float frac = capacity > 0 ? (float)((double)used / (double)capacity) : 0.0f;
                char overlay[96];
                snprintf(overlay, sizeof(overlay), "%.2f / %.2f MB  (%u / %u)",
                    bytes(used, stride) * toMB, bytes(capacity, stride) * toMB, used, capacity);
                ImGui::Text("%s", label);
                ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);
            };

        ImGui::SeparatorText("Buffers");
        bar("meshes", ms->nextMeshOffset, ms->meshesAllocatedCount, sizeof(HLSL::Mesh));
        bar("meshlets", ms->nextMeshletOffset, ms->meshletsAllocatedCount, sizeof(HLSL::Meshlet));
        bar("meshlet vertices", ms->nextMeshletVertexOffset, ms->meshletVerticesAllocatedCount, sizeof(unsigned int));
        bar("meshlet triangles", ms->nextMeshletTriangleOffset, ms->meshletTrianglesAllocatedCount, sizeof(unsigned char));
        bar("vertices", ms->nextVertexOffset, ms->verticesAllocatedCount, sizeof(VertexPacked));
        bar("indices", ms->nextIndexOffset, ms->indicesAllocatedCount, sizeof(unsigned int));

        double totalUsed =
            bytes(ms->nextMeshOffset, sizeof(HLSL::Mesh)) +
            bytes(ms->nextMeshletOffset, sizeof(HLSL::Meshlet)) +
            bytes(ms->nextMeshletVertexOffset, sizeof(unsigned int)) +
            bytes(ms->nextMeshletTriangleOffset, sizeof(unsigned char)) +
            bytes(ms->nextVertexOffset, sizeof(VertexPacked)) +
            bytes(ms->nextIndexOffset, sizeof(unsigned int));
        double totalCap =
            bytes(ms->meshesAllocatedCount, sizeof(HLSL::Mesh)) +
            bytes(ms->meshletsAllocatedCount, sizeof(HLSL::Meshlet)) +
            bytes(ms->meshletVerticesAllocatedCount, sizeof(unsigned int)) +
            bytes(ms->meshletTrianglesAllocatedCount, sizeof(unsigned char)) +
            bytes(ms->verticesAllocatedCount, sizeof(VertexPacked)) +
            bytes(ms->indicesAllocatedCount, sizeof(unsigned int));

        ImGui::Separator();
        ImGui::Text("Total %.2f / %.2f MB across %zu meshes",
            totalUsed * toMB, totalCap * toMB, ms->allMeshes.size());

        ImGui::SeparatorText("BLAS scratch");
        uint scratchPeakFrame = ms->scratchPeakLastFrame.load(std::memory_order_relaxed);
        uint scratchPeakEver = ms->scratchPeakEver.load(std::memory_order_relaxed);
        bar("last frame", scratchPeakFrame, ms->maxScratchSizeInBytes, 1);
        ImGui::Text("session peak %.2f MB (%.1f%% of the %.0f MB reserved)",
            scratchPeakEver * toMB,
            ms->maxScratchSizeInBytes > 0 ? 100.0 * scratchPeakEver / ms->maxScratchSizeInBytes : 0.0,
            ms->maxScratchSizeInBytes * toMB);
        ImGui::Text("extra batches last frame %u", ms->scratchBatchesLastFrame.load(std::memory_order_relaxed));

        DrawASTable(ms, toMB);

        // Visual layout of the vertices buffer: each mesh occupies [vertexOffset, +vertexCount].
        ImGui::SeparatorText("Vertex buffer layout");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;
        float height = 24.0f;
        ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);
        dl->AddRectFilled(p0, p1, IM_COL32(40, 40, 40, 255));

        uint cap = ms->verticesAllocatedCount;
        if (cap > 0)
        {
            // allMeshes is a sparse unordered_map (holds both regular and override meshes,
            // recycled slots leave holes) -- iterate it directly rather than by numeric index.
            uint i = 0;
            for (auto& kv : ms->allMeshes)
            {
                auto& m = kv.second;
                float x0 = p0.x + width * ((float)m.vertexOffset / cap);
                float x1 = p0.x + width * ((float)(m.vertexOffset + m.vertexCount) / cap);
                ImU32 col = (ImU32)ImColor::HSV((float)((i * 0.137f) - (int)(i * 0.137f)), 0.6f, 0.85f);
                dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(max(x1, x0 + 1.0f), p1.y), col);
                i++;
            }
        }
        dl->AddRect(p0, p1, IM_COL32(120, 120, 120, 255));
        ImGui::Dummy(ImVec2(width, height));

        ImGui::End();
    }
};
MeshStorageWindow meshStorageWindow;

class ShaderBucketsWindow : public EditorWindow
{
public:
    ShaderBucketsWindow() : EditorWindow("ShaderBuckets") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("ShaderBucketsWindow", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        if (Renderer::instance == nullptr)
        {
            ImGui::TextDisabled("Renderer not initialized.");
            ImGui::End();
            return;
        }

        // Snapshot under the registry's own lock (see ShaderBucketRegistry::GetDebugStats) --
        // separate from BuildFrameSnapshot, which the render loop uses every frame and must
        // stay minimal, so this window carries its own (slightly heavier) per-bucket walk.
        std::vector<ShaderBucketDebugEntry> stats = Renderer::instance->mainView.viewWorld.shaderBucketRegistry.GetDebugStats();

        uint totalMeshlets = 0;
        uint totalInstances = 0;
        for (auto& s : stats)
        {
            totalMeshlets += s.meshletCount;
            totalInstances += s.instanceCount;
        }
        ImGui::Text("%zu buckets, %u meshlets, %u instances", stats.size(), totalMeshlets, totalInstances);
        ImGui::Separator();

        if (ImGui::BeginTable("##ShaderBuckets", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Bucket", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Shader");
            ImGui::TableSetupColumn("Meshlets", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Instances", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            for (auto& s : stats)
            {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%u", s.bucketIndex);

                ImGui::TableNextColumn();
                if (AssetLibrary::instance != nullptr && AssetLibrary::instance->map.contains(s.shaderAssetId))
                    ImGui::TextUnformatted(AssetLibrary::instance->map[s.shaderAssetId].path.c_str());
                else
                    ImGui::TextDisabled("<unknown %llu>", (unsigned long long)s.shaderAssetId.hash);

                ImGui::TableNextColumn();
                ImGui::Text("%u", s.meshletCount);

                ImGui::TableNextColumn();
                ImGui::Text("%u", s.instanceCount);
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }
};
ShaderBucketsWindow shaderBucketsWindow;

class HandlePickingWindow : public EditorWindow
{
    struct HandleEntry
    {
        EntityBase handle;
        String name;
    };
    std::vector<HandleEntry> entries;

    EntityBase* handle;
    World::Entity selectedEntity = entityInvalid;
public:
    HandlePickingWindow() : EditorWindow("HandlePicking") { isOpen = false; }
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
            selected = selectedEntity == entries[i].handle;
            if (selected)
            {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), entries[i].name.c_str());
            }
            else
            {
                ImGui::Selectable(entries[i].name.c_str(), &selected);
                if (selected)
                    selectedEntity = entries[i].handle;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        if (ImGui::Button("Select"))
        {
            *handle = selectedEntity;
            Close();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            Close();
        }

        ImGui::End();
    }

    void Open(Components::Mask mask, EntityBase* _handle)
    {
        uint resCount = World::instance->CountQuery(mask, 0);
        uint instanceQueryIndex = World::instance->Query(mask, 0, false);
        auto& result = World::instance->frameQueries[instanceQueryIndex];

        entries.clear();
        for (uint i = 0; i < result.size(); i++)
        {
            HandleEntry entry;
            World::Entity entity = { result[i].Get<Components::Entity>() };
            if (entity.Has<Components::Name>())
            {
                entry.name = entity.Get<Components::Name>().name;
            }
            else
            {
                entry.name = "--";
            }
            entry.handle = entity;
            entries.push_back(entry);
        }
        handle = _handle;
        selectedEntity = *handle;
        isOpen = true;
    }

    void Close()
    {
        handle = nullptr;
        isOpen = false;
    }

    bool KeyEvent() override
    {
        if (IOs::instance->keys.pressed[VK_ESCAPE])
        {
            Close();
            return true;
        }
        return false;
    }
};
HandlePickingWindow handlePickingWindow;

class FileBrowserWindow : public EditorWindow
{
    String OriginalPath;
    String NavigationGuess;
    String EditedPath;
    String Selected;
    String RenameResult;
    std::set<String> Ext;

    enum class FileBrowserOption
    {
        DIRECTORY,
        FILE
    };
    FileBrowserOption Option = FileBrowserOption::FILE;

    const float Width = 500.0f;
    const float Height = 400.0f;

    std::vector<String> Directories;
    std::vector<String> Files;

    const float doubleClickTime = 200; // ms
    float doubleClickTimer; // ms
    bool selectedByDoubleClick = false;

    // Save-As mode: instead of only picking an existing file, the user edits a destination
    // filename (pre-filled with a proposal). Select returns <currentDir>\<saveName>.
    bool saveMode = false;
    char saveName[256] = {};

    typedef void (*callback)(String);
    callback resultCB;

public:
    FileBrowserWindow() : EditorWindow("FileBrowser") { isOpen = false; OriginalPath = ""; }

    bool Open(callback result)
    {
        resultCB = result;
        return Open(FileBrowserOption::FILE, {});
    }

    bool Open(FileBrowserOption InOption, const std::set<String>& InExt)
    {
        if (OriginalPath.empty())
            OriginalPath = std::filesystem::current_path().string();

        // Setup
        isOpen = true;
        Option = InOption;
        Ext = InExt;
        saveMode = false;
        selectedByDoubleClick = false;
        TryApplyPath(OriginalPath);

        return true;
    }

    // "Save As" flavor: opens at initialDir with proposedFile pre-filled in an editable name
    // field, filtered by InExt. On Select, result() receives "<currentDir>\<name>" (or "" if the
    // name is blank). Pure ImGui -> safe to drive from the editor (worker) thread.
    bool OpenSave(const String& initialDir, const String& proposedFile, const std::set<String>& InExt, callback result)
    {
        resultCB = result;
        isOpen = true;
        Option = FileBrowserOption::FILE;
        saveMode = true;
        Ext = InExt;
        selectedByDoubleClick = false;
        OriginalPath = initialDir.empty() ? String(std::filesystem::current_path().string()) : initialDir;
        TryApplyPath(OriginalPath);
        strncpy_s(saveName, proposedFile.c_str(), _TRUNCATE); // pre-fill the destination name
        return true;
    }

    // "Open" flavor: pick one existing file at/under initialDir, filtered by InExt. On Select (or
    // double-click), result() receives "<currentDir>\<file>" (or "" if nothing picked). Pure ImGui.
    bool OpenLoad(const String& initialDir, const std::set<String>& InExt, callback result)
    {
        resultCB = result;
        isOpen = true;
        Option = FileBrowserOption::FILE;
        saveMode = false;
        Ext = InExt;
        selectedByDoubleClick = false;
        OriginalPath = initialDir.empty() ? String(std::filesystem::current_path().string()) : initialDir;
        TryApplyPath(OriginalPath);
        return true;
    }

    bool FetchInternal()
    {
        if (!isOpen)
            return false;

        doubleClickTimer += Time::instance->deltaSeconds * 1000;

        String OutSelectedPath;

        bool result = false;
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowSize({ Width, Height });
        ImVec2 pos = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
        if (ImGui::Begin(GetLabel().c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            EditNavigation();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            EditContent();
            ImGui::Spacing();

            // Editable destination filename (Save-As only)
            if (saveMode)
            {
                ImGui::Text("Name:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##SaveName", saveName, sizeof(saveName));
            }

            // Extension text in the bottom right
            String ext;
            switch (Option)
            {
            case FileBrowserOption::DIRECTORY:
                ext = "Directory";
                break;
            case FileBrowserOption::FILE:
                for (auto e : Ext)
                {
                    if (!ext.empty())
                        ext += ", ";
                    ext += e;
                }
                break;
            }
            if (!ext.empty())
            {
                ImGui::Text((" Ext: " + ext).c_str());
                ImGui::SameLine();
            }

            // Select / Cancel buttons
            const ImGuiStyle style = ImGui::GetStyle();
            constexpr ImVec2 buttonSize(100.f, 0.f);
            const float widthNeeded = buttonSize.x + style.ItemSpacing.x + buttonSize.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - widthNeeded);
            if (ImGui::Button("Cancel", buttonSize))
            {
                OutSelectedPath = "";
                result = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Select", buttonSize) || selectedByDoubleClick)
            {
                switch (Option)
                {
                case FileBrowserOption::DIRECTORY:
                    OutSelectedPath = EditedPath;
                    break;
                case FileBrowserOption::FILE:
                    if (saveMode)
                        OutSelectedPath = (saveName[0] == 0) ? "" : EditedPath + "\\" + saveName;
                    else
                        OutSelectedPath = Selected.empty() ? "" : EditedPath + "\\" + Selected;
                    break;
                }
                result = true;
                OriginalPath = EditedPath; // keep the last directory ?
                resultCB(OutSelectedPath);
            }

            ImGui::End();
        }

        return result;
    }

    String GetLabel() const
    {
        return "File Browser##" + OriginalPath;
    }

    void Refresh()
    {
        // 1. Find current directory
        const std::filesystem::path path(EditedPath.c_str());
        if (!std::filesystem::exists(path))
        {
            TryPopPath();
            return;
        }

        // 2. Store directory content
        Directories.clear();
        Files.clear();
        for (auto& entry : std::filesystem::directory_iterator(path))
        {
            if (entry.is_directory())
            {
                Directories.push_back(entry.path().filename().string());
            }
            else if (Option != FileBrowserOption::DIRECTORY)
            {
                // Filter extensions
                if (!Ext.empty())
                {
                    if (!entry.path().has_extension())
                        continue;
                    const String ext = entry.path().extension().string();
                    if (!Ext.contains(ext))
                        continue;
                }
                Files.push_back(entry.path().filename().string());
            }
        }

        Selected = "";
        RenameResult = "";
        NavigationGuess = "";
    }

    bool TryPopPath()
    {
        // Pop until exists or at root
        std::filesystem::path path(EditedPath.c_str());
        while (!std::filesystem::exists(path) && path.has_parent_path())
            path = path.parent_path();
        return TryApplyPath(path.parent_path().string());
    }

    bool TryApplyPath(const String& InString)
    {
        EditedPath = InString;
        Refresh();
        return true;
    }

    void EditNavigation()
    {
        constexpr ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_CharsNoBlank |
            ImGuiInputTextFlags_AutoSelectAll |
            ImGuiInputTextFlags_EnterReturnsTrue;
        //|ImGuiInputTextFlags_CallbackCompletion |
        //ImGuiInputTextFlags_CallbackHistory;
        if (ImGui::InputText("##Path", EditedPath.data(), EditedPath.capacity() + 1, flags))
        {
            EditedPath == ".." ?
                TryPopPath() :
                TryApplyPath(EditedPath);
        }

        ImGui::SameLine();
        if (ImGui::Button("^"))
            TryPopPath();
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            Refresh();
    }
    void EditContent()
    {
        constexpr ImVec2 buttonSize(100.f, 0.f);

        const ImGuiStyle style = ImGui::GetStyle();
        const ImVec2 size = {
            Width - style.WindowPadding.x * 2.0f,
            Height - style.WindowPadding.y * 2.0f - (saveMode ? 143.0f : 115.0f) // extra row for the name field
        };
        if (ImGui::BeginListBox("##FileBrowserContent", size))
        {
            ImGui::Spacing();
            ImGui::Spacing();

            if (!EditedPath.empty())
                if (ImGui::Selectable("  ..##FileListBack"))
                    TryPopPath();

            String newDir;
            for (auto& dir : Directories)
            {
                if (ContentEntry(dir, true))
                {
                    if (dir == Selected && (doubleClickTimer < doubleClickTime))
                    {
                        newDir = dir;
                    }
                    else
                    {
                        Selected = dir;
                    }
                    doubleClickTimer = 0;
                }
            }

            for (auto& file : Files)
            {
                if (ContentEntry(file, false))
                {
                    const bool isDoubleClick = (file == Selected) && (doubleClickTimer < doubleClickTime);
                    Selected = file;
                    if (saveMode)
                        strncpy_s(saveName, file.c_str(), _TRUNCATE); // clicking a file fills the name to overwrite it
                    if (isDoubleClick)
                        selectedByDoubleClick = true; // validate: load it, or overwrite it in save mode
                    doubleClickTimer = 0;
                }
            }

            if (!newDir.empty())
                TryApplyPath(EditedPath + "\\" + newDir);

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::EndListBox();
        }
    }

    bool ContentEntry(const String& InEntry, bool InIsDir)
    {
        const bool selected = Selected == InEntry;
        return ImGui::Selectable(((InIsDir ? "- " : "  ") + InEntry + "##ListEntry").c_str(), selected);
    }

    void Update() override final
    {
        ZoneScoped;
        if (FetchInternal())
            Close();
    }

    bool KeyEvent() override
    {
        if (IOs::instance->keys.pressed[VK_ESCAPE])
        {
            Close();
            return true;
        }
        return false;
    }
};
FileBrowserWindow fileBrowserWindow;
// Set right before opening fileBrowserWindow for a texture import (DrawHandle below); the
// FileBrowserWindow result callback is a plain function pointer (no captures), so the target
// handle to write into rides on this global instead. Cleared once consumed.
static World::Entity* pendingTextureImportTarget = nullptr;

// Forward-declared in ComponentMetaTypes.h. Draws every reflected member of the component
// found in knownComponents for `mask`, exactly like the property window's path for components
// with no custom PropertyDraw -- callable from a custom PropertyDraw that wants the generic
// rendering plus its own controls (e.g. Components::InstancePropertyDraw, in World.h).
// Adds `mask` to the entity if it doesn't already have it, leaving the new component ZEROED with all
// its Handles reset to entityInvalid. Returns true only when it actually added something.
//
// The zeroing is not optional: component pools are raw malloc and nothing runs the default member
// initializers (World::Pool::On allocates, GetSlot just bumps a counter), so a freshly added
// component starts as garbage -- or as a released entity's leftovers. The Handle pass matters just as
// much, because a *zeroed* handle is id 0 / rev 0, which resolves to a REAL entity and is worse than
// garbage. Proper fix is TODO 18 (default values in the component metadata); until then the caller
// has to write meaningful defaults itself, since zero is rarely a neutral value.
bool AddComponentIfMissing(World::Entity ent, Components::Mask mask)
{
    if (!ent.IsValid() || (ent.GetMask() & mask) != 0)
        return false;

    ComponentInfo* info = nullptr;
    for (uint i = 0; i < knownComponents.size(); i++)
    {
        if (knownComponents[i].mask == mask)
        {
            info = &knownComponents[i];
            break;
        }
    }
    if (info == nullptr)
        return false;

    ent.Add(mask);
    InitComponentData(ent, *info);
    return true;
}

// A component's slot is not constructed (Entity::Make / Add reuse pool memory as is, possibly a freed
// entity's bytes): zero it, invalidate its handles, and give the components that need it a valid default
void InitComponentData(World::Entity ent, const ComponentInfo& info)
{
    uint bucket = Components::MaskToBucket(info.mask);
    char* data = ent.Get(bucket);
    memset(data, 0, Components::strides[bucket]);
    for (uint m = 0; m < (uint)info.members.size(); m++)
    {
        auto& member = info.members[m];
        if (member.dataType != PropertyTypes::_Handle)
            continue;
        int elementStride = (member.dataCount > 0) ? (member.size / member.dataCount) : member.size;
        for (int dc = 0; dc < member.dataCount; dc++)
            *(EntityBase*)(data + member.offset + dc * elementStride) = entityInvalid;
    }
    if (info.mask == Components::Transform::mask)
    {
        auto& transform = ent.Get<Components::Transform>();
        transform.rotation = quaternion::identity();
        transform.scale = float3(1);
    }
    if (info.mask == Components::HazeLayer::mask)
        ent.Get<Components::HazeLayer>() = Components::HazeLayer();
}

// "new Entity": every component of the new entity except the ones Make itself fills
void InitNewEntityComponents(World::Entity ent)
{
    Components::Mask filled = Components::Entity::mask | Components::State::mask | Components::Name::mask;
    for (auto& info : knownComponents)
        if ((ent.GetMask() & info.mask) != 0 && (info.mask & filled) == 0)
            InitComponentData(ent, info);
}

// The two collider presets on the property window's button row. "Static" is a static body with a
// concave Mesh collider, "Dynamic" is a dynamic body with a ConvexHull -- the only two combinations
// Jolt accepts without falling back, since it cannot simulate a concave mesh as a moving body.
// Existing components keep whatever the user tuned; only what the preset is about is overwritten.
void SetPhysicsPreset(World::Entity ent, uint motionType, uint shapeType)
{
    if (!ent.IsValid())
        return;
    if (!ent.Has<Components::Transform>())
    {
        IOs::Log("Physics preset: the selected entity has no Transform -- a rigid body needs one, nothing changed");
        return;
    }

    // Each Add invalidates every component reference on this entity (Entity::Add -> Copy), so the
    // defaults are written inside their own scope and everything is re-Get after both Adds.
    if (AddComponentIfMissing(ent, Components::RigidBody::mask))
    {
        auto& rigidBody = ent.Get<Components::RigidBody>();
        rigidBody.objectLayer = 0;
        rigidBody.mass = 1.0f;
        rigidBody.friction = 0.2f;
        rigidBody.restitution = 0.0f;
        rigidBody.linearDamping = 0.05f;
        rigidBody.angularDamping = 0.05f;
        rigidBody.gravityFactor = 1.0f;
        rigidBody.allowSleeping = 1;
        rigidBody.isSensor = 0;
    }
    if (AddComponentIfMissing(ent, Components::CollisionShape::mask))
    {
        auto& collisionShape = ent.Get<Components::CollisionShape>();
        collisionShape.halfExtent = float3(0.5f, 0.5f, 0.5f);
        collisionShape.offset = float3(0.0f, 0.0f, 0.0f);
        collisionShape.convexRadius = 0.05f;
    }

    ent.Get<Components::RigidBody>().motionType = motionType;

    auto& collisionShape = ent.Get<Components::CollisionShape>();
    collisionShape.shapeType = shapeType;
    // Both presets are mesh-derived, so the collider comes from whatever the entity renders.
    if (ent.Has<Components::Instance>())
        collisionShape.mesh = ent.Get<Components::Instance>().mesh;

    // Sync recreates the body on a motionType change and SetShapes it on a shapeType change.
    auto& state = ent.Get<Components::State>();
    state.flags |= Components::State::Flags::physicsDirty;
    state.flags |= Components::State::Flags::instanceDirty;
}

// The authored numbers that separate one vehicle preset from another. Everything MakeVehicle does
// with them -- the component masks, the axis convention, which wheel steers, which one holds the
// hand brake -- is the same for every car and is not worth a field.
struct VehiclePreset
{
    const char* name;

    // Chassis box half extents. X = width, Y = height, Z = length, the forward axis.
    float halfWidth;
    float halfHeight;
    float halfLength;
    float chassisOffsetY;           // box center relative to the entity origin. Negative drops the
                                    // center of mass with it, which is what keeps a car off its roof
    float convexRadius;             // box bevel; BuildShape clamps it to half the smallest extent
    float spawnHeight;              // room for the suspension to settle instead of starting buried
    float mass;                     // kg

    // Front and rear authored apart, because on anything but a toy they differ: a buggy runs fatter
    // rear tires than front, a different track each end, and axles that are not symmetric about the
    // body origin. Authored outright rather than derived from the chassis box -- Jolt's sample puts
    // the axles at halfLength - 2 * wheelRadius, which only holds while the tires are small next to
    // the body: on a buggy that rule gives a 500 mm wheelbase against a 330 mm class limit.
    float halfTrackFront;           // |X| of a front wheel center
    float halfTrackRear;
    float axleZFront;               // signed Z of the front axle, chassis local
    float axleZRear;                // signed Z of the rear axle, so negative on a normal car
    float wheelAttachY;             // Y of the suspension hard point, chassis local
    float wheelRadius;
    float wheelWidthFront;
    float wheelWidthRear;
    float suspensionMinLength;
    float suspensionMaxLength;
    // Static sag is g / (2*pi*f)^2, which does NOT scale with the vehicle: 1.5 Hz always sags
    // 110 mm. A car's 200 mm of travel swallows that; a buggy's 40 mm does not, and it would sit on
    // its bump stops before it ever moved. Shrinking a vehicle means raising this.
    float suspensionFrequency;      // Hz
    float wheelInertia;             // kg m^2, 0.5 * wheelMass * radius^2
    float maxBrakeTorque;           // Nm, has to beat friction * wheel load * radius to lock a wheel
    float maxHandBrakeTorque;       // Nm, rear axle only

    float engineMaxTorque;          // Nm at the crank, multiplied by the gear and differentialRatio
    float engineMaxRPM;
    float engineInertia;            // kg m^2
    float clutchStrength;           // kg m^2 / s, sized by the engine rather than by the car
    float differentialRatio;        // axle reduction; multiplies the gear ratio into wheel torque
    uint  collisionTester;          // 0 = ray, 1 = sphere, 2 = cylinder
};

// Roughly the proportions of Jolt's own vehicle sample, which is what its default engine,
// suspension and friction numbers were tuned against: a 4 m, 1500 kg road car.
static constexpr VehiclePreset vehiclePresetCar =
{
    .name = "Vehicle",
    .halfWidth = 0.9f,
    .halfHeight = 0.2f,
    .halfLength = 2.0f,
    .chassisOffsetY = 0.0f,
    .convexRadius = 0.05f,
    .spawnHeight = 2.0f,
    .mass = 1500.0f,
    .halfTrackFront = 0.9f,
    .halfTrackRear = 0.9f,
    .axleZFront = 1.4f,             // halfLength - 2 * wheelRadius, the sample's own placement
    .axleZRear = -1.4f,
    .wheelAttachY = -0.18f,         // -0.9 * halfHeight
    .wheelRadius = 0.3f,
    .wheelWidthFront = 0.1f,
    .wheelWidthRear = 0.1f,
    .suspensionMinLength = 0.3f,
    .suspensionMaxLength = 0.5f,
    .suspensionFrequency = 1.5f,
    .wheelInertia = 0.9f,
    .maxBrakeTorque = 1500.0f,
    .maxHandBrakeTorque = 4000.0f,
    .engineMaxTorque = 500.0f,
    .engineMaxRPM = 6000.0f,
    .engineInertia = 0.5f,
    .clutchStrength = 10.0f,
    .differentialRatio = 3.42f,
    .collisionTester = 0,
};

// 1/8 scale electric off-road buggy. The class rules bound the envelope -- 730 mm long, 310 mm wide,
// a wheelbase of 270-330 mm, 3200 g minimum -- and the dimensions below stay inside them, but the
// numbers themselves came off a tuning pass in the property window, not off the rule sheet, so a few
// (mass, tire diameter) sit under the legal floor on purpose. The chassis box is the BODY: that
// 730 x 310 envelope is measured over the bumper, the rear wing and the tires, none of which are
// colliders here.
static constexpr VehiclePreset vehiclePresetBuggy =
{
    .name = "Buggy",
    .halfWidth = 0.07f,             // 140 mm body, clear of the inner tire faces at both ends
    .halfHeight = 0.045f,           // 90 mm
    .halfLength = 0.20f,            // 400 mm of the 730 mm envelope
    .chassisOffsetY = -0.023f,      // body slung below the axle line, and the mass with it
    .convexRadius = 0.01f,          // 0.05 would be bigger than halfHeight
    .spawnHeight = 0.3f,
    .mass = 3.0f,                   // by feel; the real class floor is 3.2 kg
    .halfTrackFront = 0.13f,        // 295 mm over the front tires, against a 310 mm limit
    .halfTrackRear = 0.12f,         // 285 mm over the rear
    .axleZFront = 0.17f,
    .axleZRear = -0.157f,           // 327 mm wheelbase, just inside the legal 330
    .wheelAttachY = 0.02f,          // upper shock mount, near the top of the body
    .wheelRadius = 0.051f,          // 102 mm tire, a little under the legal 109.22 minimum
    .wheelWidthFront = 0.035f,      // narrow front, fat rear: the usual off-road split
    .wheelWidthRear = 0.045f,
    .suspensionMinLength = 0.025f,
    .suspensionMaxLength = 0.1f,  // 40 mm of travel
    .suspensionFrequency = 4.0f,    // stiff: 4 mm of static sag, so it rests near full droop
    .wheelInertia = 0.0003f,        // 0.5 * 180 g * radius^2. Leave it POSITIVE -- a zero here makes
                                    // PhysicsAuthoredPositive fall back to 0.9, a road car's wheel
    .maxBrakeTorque = 2.0f,         // a wheel locks at about 0.5 Nm at this weight
    .maxHandBrakeTorque = 4.0f,
    .engineMaxTorque = 1.0f,      // far past traction; the tires give up long before the engine
    .engineMaxRPM = 6000.0f,
    .engineInertia = 0.001f,
    .clutchStrength = 1.0f,
    .differentialRatio = 1.0f,      // 1:1, no axle reduction -- this one gets its speed from revs
    .collisionTester = 1,           // sphere: a 51 mm ray-cast wheel drops into cracks a tire rolls over
};

// "New Entity > Vehicle" and "> Buggy": a complete, drivable car in one click -- a dynamic box
// chassis carrying the Vehicle component, plus the four child entities carrying Wheel that
// Physics::SyncVehicles looks for through Parent (Physic.h).
//
// Every field of every component is assigned on purpose: Entity::Make hands back RAW POOL MEMORY
// (it only clears State), so anything left out would be garbage, not the declared default.
//
// The car renders nothing -- there is no built-in box or wheel mesh -- so it shows up through
// drawPhysicsShapes / drawPhysicsConstraints until an Instance is hung on the chassis and wheels.
void MakeVehicle(const VehiclePreset& preset)
{
    World::Entity chassis;
    chassis.Make(Components::Transform::mask | Components::RigidBody::mask | Components::CollisionShape::mask | Components::Vehicle::mask, preset.name);

    auto& transform = chassis.Get<Components::Transform>();
    transform.position = float3(0, preset.spawnHeight, 0);
    transform.rotation = quaternion::identity();
    transform.scale = float3(1);
    transform.version = 0;

    auto& rigidBody = chassis.Get<Components::RigidBody>();
    rigidBody.motionType = 2;               // Dynamic: the only kind Jolt will drive
    rigidBody.objectLayer = 0;
    rigidBody.mass = preset.mass;
    rigidBody.friction = 0.2f;
    rigidBody.restitution = 0.0f;
    rigidBody.linearDamping = 0.05f;
    rigidBody.angularDamping = 0.05f;
    rigidBody.gravityFactor = 1.0f;
    rigidBody.allowSleeping = 1;
    rigidBody.isSensor = 0;

    auto& collisionShape = chassis.Get<Components::CollisionShape>();
    collisionShape.shapeType = 0;           // Box
    collisionShape.mesh = Components::Handle<Components::Mesh>{ entityInvalid };
    collisionShape.halfExtent = float3(preset.halfWidth, preset.halfHeight, preset.halfLength);
    collisionShape.offset = float3(0, preset.chassisOffsetY, 0);
    collisionShape.convexRadius = preset.convexRadius;

    auto& vehicle = chassis.Get<Components::Vehicle>();
    vehicle.up = float3(0, 1, 0);
    vehicle.forward = float3(0, 0, 1);
    vehicle.maxPitchRollAngle = 3.14159265f;    // pi = never righted by the constraint
    vehicle.collisionTester = preset.collisionTester;
    vehicle.maxSlopeAngle = 1.3962634f;         // 80 deg
    vehicle.engineMaxTorque = preset.engineMaxTorque;
    vehicle.engineMinRPM = 1000.0f;
    vehicle.engineMaxRPM = preset.engineMaxRPM;
    vehicle.engineInertia = preset.engineInertia;
    vehicle.shiftUpRPM = 4000.0f;
    vehicle.shiftDownRPM = 2000.0f;
    vehicle.clutchStrength = preset.clutchStrength;
    vehicle.differentialRatio = preset.differentialRatio;
    vehicle.forwardInput = 0.0f;
    vehicle.rightInput = 0.0f;
    vehicle.brakeInput = 0.0f;
    vehicle.handBrakeInput = 0.0f;

    // SeeD is LEFT-handed -- +X right, +Y up, +Z forward, the axes the camera's WASD keys move along
    // (World.h) -- so a wheel on the car's LEFT sits at -X. Which side a wheel is on only picks its
    // slot in the axle's differential, but the names may as well tell the truth.
    const char* wheelNames[4] = { "Wheel Front Left", "Wheel Front Right", "Wheel Rear Left", "Wheel Rear Right" };
    for (uint i = 0; i < 4; i++)
    {
        bool front = i < 2;
        bool left = (i % 2) == 0;

        World::Entity wheelEnt;
        wheelEnt.Make(Components::Transform::mask | Components::Parent::mask | Components::Wheel::mask, wheelNames[i]);
        wheelEnt.Get<Components::Parent>().entity.Set(chassis);

        float halfTrack = front ? preset.halfTrackFront : preset.halfTrackRear;
        float3 attachment = float3(
            left ? -halfTrack : halfTrack,
            preset.wheelAttachY,
            front ? preset.axleZFront : preset.axleZRear);

        // Output only -- WriteBackWheelTransforms overwrites it every frame from the simulated
        // pose. Seeded with the attachment point so the hierarchy looks right before the first step.
        auto& wheelTransform = wheelEnt.Get<Components::Transform>();
        wheelTransform.position = attachment;
        wheelTransform.rotation = quaternion::identity();
        wheelTransform.scale = float3(1);
        wheelTransform.version = 0;

        auto& wheel = wheelEnt.Get<Components::Wheel>();
        wheel.position = attachment;                        // the suspension hard point, chassis local space
        wheel.suspensionDirection = float3(0, -1, 0);
        wheel.radius = preset.wheelRadius;
        wheel.width = front ? preset.wheelWidthFront : preset.wheelWidthRear;
        wheel.suspensionMinLength = preset.suspensionMinLength;
        wheel.suspensionMaxLength = preset.suspensionMaxLength;
        wheel.suspensionFrequency = preset.suspensionFrequency;
        wheel.suspensionDamping = 0.5f;
        wheel.maxSteerAngle = front ? 0.5235988f : 0.0f;    // 30 deg, front axle only
        wheel.maxBrakeTorque = preset.maxBrakeTorque;
        wheel.maxHandBrakeTorque = front ? 0.0f : preset.maxHandBrakeTorque;   // the hand brake locks the rear
        wheel.inertia = preset.wheelInertia;
        wheel.angularDamping = 0.2f;
        wheel.axle = front ? 0 : 1;
        wheel.driven = 1;                                   // four wheel drive: the forgiving default
    }

    editorState.selectedObject = chassis;
    editorState.dirtyHierarchy = true;
}

void MakeDefaultVehicle() { MakeVehicle(vehiclePresetCar); }
void MakeDefaultBuggy() { MakeVehicle(vehiclePresetBuggy); }

void DefaultPropertyDraw(Components::Mask mask, char* cmpData)
{
    ComponentInfo* metaData = nullptr;
    for (uint i = 0; i < knownComponents.size(); i++)
    {
        if (knownComponents[i].mask == mask)
        {
            metaData = &knownComponents[i];
            break;
        }
    }
    if (metaData == nullptr)
        return;

    uint pushID = 0;
    for (uint memberIndex = 0; memberIndex < metaData->members.size(); memberIndex++)
    {
        ImGui::PushID(pushID++);
        auto& m = metaData->members[memberIndex];
        char* data = cmpData + m.offset;
        // Element stride from the metadata rather than the C++ element type: hlslpp's float3 and
        // float4x4 are wider than the floats they expose, so indexing them as a float array walks
        // off the element boundary.
        int elementStride = (m.dataCount > 0) ? (m.size / m.dataCount) : m.size;
        ImGui::Text(m.name.c_str());
        if (m.dataCount == 1)
            ImGui::SameLine();
        for (int dc = 0; dc < m.dataCount; dc++)
        {
            ImGui::PushID(pushID++);
            char* element = data + dc * elementStride;
            switch (m.dataType)
            {
            case PropertyTypes::_int:
            case PropertyTypes::_uint:
                ImGui::InputInt("", (int*)element);
                break;
            case PropertyTypes::_float:
                ImGui::DragFloat("", (float*)element);
                break;
            case PropertyTypes::_float2:
                ImGui::DragFloat2("", (float*)element);
                break;
            case PropertyTypes::_float3:
                ImGui::DragFloat3("", (float*)element);
                break;
            case PropertyTypes::_float4:
                ImGui::DragFloat4("", (float*)element);
                break;
            case PropertyTypes::_quaternion:
                ImGui::DragFloat4("", (float*)element);
                break;
            case PropertyTypes::_float4x4:
            {
                float4x4& mat = *(float4x4*)element;
                ImGui::DragFloat4("x", (float*)&mat[0]);
                ImGui::DragFloat4("y", (float*)&mat[1]);
                ImGui::DragFloat4("z", (float*)&mat[2]);
                ImGui::DragFloat4("pos", (float*)&mat[3]);
            }
            break;
            case PropertyTypes::_assetID:
            {
                assetID id = *(assetID*)element;
                if (AssetLibrary::instance->map.contains(id))
                {
                    if (ImGui::SmallButton("o"))
                    {
                        fileBrowserWindow.Open([](String selectedPath)
                            {
                                int toto = 0;
                                //add the path to the assetlib
                                //assign the new id to the assetID
                            }
                        );
                    }
                    ImGui::SameLine();
                    ImGui::Text(AssetLibrary::instance->map[id].originalFilePath.c_str());
                }
            }
            break;
            case PropertyTypes::_Handle:
            {
                UIHelpers::DrawHandle(*(EntityBase*)element, m.dataTemplateType);
            }
            break;
            case PropertyTypes::_bool:
                ImGui::Checkbox("", (bool*)element);
                break;
            default:
                break;
            }

            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

namespace Components
{
    // Shader picker (declared in World.h). A shader asset path is "<file.hlsl>|<PublicName>", and
    // PublicName is declared by a "#pragma <stage> <PublicName> <entry...>" line in that file --
    // so instead of typing the string, pick the file in one combo and one of the variations it
    // declares in the other. ShaderLoader::ListVariations reads those pragmas with the very
    // parser the compiler uses, so a listed variation always compiles.
    // Only `path` is written: the renderer re-Adds it to the AssetLibrary under the component's
    // EXISTING assetID every frame (see the material pass in Renderer.h), which is what actually
    // recompiles it. Minting a new id here would orphan the old asset entry and its shader bucket.
    static void ShaderPropertyDraw(char* p)
    {
        Shader* shader = (Shader*)p;

        struct ShaderFile
        {
            String path;
            std::vector<ShaderLoader::Variation> variations;
        };
        // Scanned once (~25 file reads). Files declaring no variation (common.hlsl, structs.hlsl,
        // ...) are dropped: picking one could only ever build an uncompilable path. Shaders
        // hot-reload, so a #pragma can appear while the editor runs -- hence the rescan button
        // rather than a cache that can only go stale.
        static std::vector<ShaderFile> shaderFiles;
        static bool scanned = false;
        if (!scanned && ShaderLoader::instance != nullptr)
        {
            scanned = true;
            std::error_code ec;
            for (auto& entry : std::filesystem::directory_iterator(ShaderLoader::shadersPath, ec))
            {
                if (entry.is_directory() || entry.path().extension() != ".hlsl")
                    continue;
                ShaderFile shaderFile;
                shaderFile.path = String(ShaderLoader::shadersPath + entry.path().filename().string());
                ShaderLoader::instance->ListVariations(shaderFile.path, shaderFile.variations);
                if (!shaderFile.variations.empty())
                    shaderFiles.push_back(shaderFile);
            }
        }

        // split the current path so both combos open on whatever is already set
        String current(shader->path);
        size_t separator = current.find("|");
        String file = separator == String::npos ? current : String(current.substr(0, separator));
        String variation = separator == String::npos ? String() : String(current.substr(separator + 1));

        ShaderFile* pickedFile = nullptr;
        for (auto& shaderFile : shaderFiles)
            if (shaderFile.path.ToLower() == file.ToLower()) // hardcoded paths don't all match the file's case
                pickedFile = &shaderFile;

        if (ImGui::BeginCombo("file", file.size() > 0 ? file.c_str() : "<none>"))
        {
            for (uint i = 0; i < (uint)shaderFiles.size(); i++)
            {
                ImGui::PushID(i);
                if (ImGui::Selectable(shaderFiles[i].path.c_str(), &shaderFiles[i] == pickedFile))
                {
                    // every listed file has at least one variation (see the scan above), so take
                    // the first: "<file>|" on its own is not a loadable path
                    String path = shaderFiles[i].path + "|" + shaderFiles[i].variations[0].name;
                    strcpy_s(shader->path, ECS_SHADER_PATH, path.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        bool variationFound = false;
        if (ImGui::BeginCombo("variation", variation.size() > 0 ? variation.c_str() : "<none>"))
        {
            if (pickedFile != nullptr)
            {
                for (uint i = 0; i < (uint)pickedFile->variations.size(); i++)
                {
                    auto& v = pickedFile->variations[i];
                    // a variation declared in an #include compiles through the includer too, but
                    // say where it came from so it doesn't look like a stray entry
                    String label = v.file == pickedFile->path
                        ? String(std::format("{} ({})", v.name.c_str(), v.stage.c_str()))
                        : String(std::format("{} ({}, from {})", v.name.c_str(), v.stage.c_str(), v.file.c_str()));
                    ImGui::PushID(i);
                    if (ImGui::Selectable(label.c_str(), v.name == variation))
                    {
                        String path = pickedFile->path + "|" + v.name;
                        strcpy_s(shader->path, ECS_SHADER_PATH, path.c_str());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        if (pickedFile != nullptr)
            for (auto& v : pickedFile->variations)
                variationFound |= v.name == variation;

        // the combos can't express every path (a shader outside src\Shaders, a file the scan
        // predates), so the raw string stays editable -- and says so when it doesn't resolve
        ImGui::InputText("path", shader->path, ECS_SHADER_PATH);
        if (!variationFound && current.size() > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), pickedFile == nullptr ? "unknown shader file" : "no such variation in that file");
        if (ImGui::SmallButton("rescan"))
        {
            shaderFiles.clear();
            scanned = false;
        }
    }
}

// InitKnownComponents() is declared in ComponentMetaData.h, included from World.h.
class PropertyWindow : public EditorWindow
{
    int componentAddIndex = 0;
    unsigned int getFirstSetBitPos(int n)
    {
        return (uint)log2(n & -n) + 1;
    }
public:
    PropertyWindow() : EditorWindow("Property") {}

    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("PropertyWindow", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        InitKnownComponents();

        if (!editorState.selectedObject.IsValid())
        {
            ImGui::End();
            return;
        }

        if (editorState.selectedObject != entityInvalid)
        {
            if (editorState.selectedObject.Has<Components::Name>())
            {
                ImGui::InputText("##Name", editorState.selectedObject.Get<Components::Name>().name, ECS_NAME_LENGTH);
            }

            if (ImGui::Button("Clone"))
            {
                World::Entity clone = editorState.selectedObject.Clone("dup", false);
                editorState.selectedObject = clone;
                editorState.dirtyHierarchy = true;
            }
            /*
            ImGui::SameLine();
            if (ImGui::Button("Clone lot"))
            {
                for (uint i = 0; i < 100000; i++)
                {
                    World::Entity clone = editorState.selectedObject.Clone("dup", false);
                    auto& trans = clone.Get<Components::Transform>();
                    trans.position.x = Rand01() * 100.0f - 50.0f;
                    trans.position.y = Rand01() * 100.0f - 50.0f;
                    trans.position.z = Rand01() * 100.0f - 50.0f;
                    editorState.selectedObject = clone;
                }
                editorState.dirtyHierarchy = true;
            }
            */
            ImGui::SameLine();
            if (ImGui::Button("Delete"))
            {
                editorState.selectedObject.Release();
                editorState.selectedObject = entityInvalid;
                editorState.dirtyHierarchy = true;

                ImGui::End();
                return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Static"))
            {
                SetPhysicsPreset(editorState.selectedObject, 0 /*Static*/, 5 /*Mesh*/);
                editorState.dirtyHierarchy = true;
            }
            ImGui::SetItemTooltip("Make this a STATIC rigid body with a concave Mesh collider, taken from its rendered mesh.\nAdds RigidBody / CollisionShape with sane defaults if missing; keeps values you already tuned.");
            ImGui::SameLine();
            if (ImGui::Button("Dynamic"))
            {
                SetPhysicsPreset(editorState.selectedObject, 2 /*Dynamic*/, 4 /*ConvexHull*/);
                editorState.dirtyHierarchy = true;
            }
            ImGui::SetItemTooltip("Make this a DYNAMIC rigid body with a ConvexHull collider, taken from its rendered mesh.\nAdds RigidBody / CollisionShape with sane defaults if missing; keeps values you already tuned.");

            uint pushID = 0;
            for (uint i = 0; i < knownComponents.size(); i++)
            {
                Components::Mask mask = knownComponents[i].mask;
                mask &= ~Components::Name::mask; // exclude name component from properties window
                mask &= ~Components::Entity::mask; // exclude entity component from properties window

                if ((editorState.selectedObject.GetMask() & mask) != 0)
                {
                    ImGui::PushID(pushID++);

                    auto& metaData = knownComponents[i];
                    char* cmpData = editorState.selectedObject.Get(Components::MaskToBucket(metaData.mask));
                    bool isopen = ImGui::CollapsingHeader(metaData.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Delete").x - 25);
                    if (ImGui::Button("Delete"))
                    {
                        editorState.selectedObject.Remove(knownComponents[i].mask);
                        ImGui::PopID();
                        editorState.dirtyHierarchy = true;
                        break;
                    }
                    if (isopen)
                    {
                        uint editStride = Components::strides[Components::MaskToBucket(metaData.mask)];
                        static std::vector<char> editSnapshot;
                        editSnapshot.assign(cmpData, cmpData + editStride);

                        if (metaData.PropertyDraw != nullptr)
                        {
                            metaData.PropertyDraw(cmpData);
                        }
                        else
                        {
                            DefaultPropertyDraw(metaData.mask, cmpData);
                        }

                        if (memcmp(editSnapshot.data(), cmpData, editStride) != 0)
                        {
                            // All of them, because this code has no idea which component it just drew.
                            // Each consumer clears its own bit.
                            auto& state = editorState.selectedObject.Get<Components::State>();
                            state.flags |= Components::State::Flags::instanceDirty;
                            state.flags |= Components::State::Flags::physicsDirty;
                            state.flags |= Components::State::Flags::constraintDirty;
                            state.flags |= Components::State::Flags::vehicleDirty;

                            // Mesh data (noRayTracing) is baked into each referencing instance's
                            // rtFlags by UpdateInstances, which only revisits dirty instances
                            if (metaData.mask == Components::Mesh::mask)
                            {
                                uint queryIndex = World::instance->Query(Components::Instance::mask, 0, false);
                                auto& result = World::instance->frameQueries[queryIndex];
                                for (uint r = 0; r < result.size(); r++)
                                {
                                    if (World::Entity(result[r].Get<Components::Instance>().mesh) == editorState.selectedObject)
                                        result[r].Get<Components::State>().flags |= Components::State::Flags::instanceDirty;
                                }
                            }
                        }

                        ImGui::PopID();
                    }
                }
            }

            ImGui::Separator();
            editorState.dirtyHierarchy |= ImGui::Combo("##AddComp", &componentAddIndex, [](void* data, int n) { return (*(std::vector<ComponentInfo>*)data)[n].name.c_str(); }, (void*)&knownComponents, (uint)knownComponents.size());
            ImGui::SameLine();
            if (ImGui::Button("add"))
            {
                AddComponentIfMissing(editorState.selectedObject, knownComponents[componentAddIndex].mask);
                editorState.dirtyHierarchy = true;
            }
        }

        ImGui::End();
    }
};
PropertyWindow propertyWindow;

#include "../../Third/ImGuizmo-master/ImGuizmo.h"
class Guizmo : public EditorWindow
{
public:
    Guizmo() : EditorWindow("Guizmo")
    {
        isOpen = true;
    }

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
            World::Entity parentEnt = par->entity.Get();
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
            rotation = normalize(MatrixToQuaternion(rotNormalized));

            matrix = mul(matrix, invParentMat);

            scale = float3(length(matrix[0].xyz), length(matrix[1].xyz), length(matrix[2].xyz));
            position = matrix[3].xyz;
        }
        t.position = position;
        t.rotation = normalize(rotation);
        t.scale = scale;
        t.version++;

        auto& state = ent.Get<Components::State>();
        state.flags |= Components::State::Flags::instanceDirty;
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
            // Only the channel the gizmo edits is taken from the decomposed matrix: reading the others back
            // every drag frame feeds the matrix rounding error into them (scale drifting while moving/rotating)
            Components::Transform before = selectedObject.Get<Components::Transform>();
            SetWorldPositionRotationScale(selectedObject, float3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]), MatrixToQuaternion(float3x3(worldMat)), float3(matrixScale[0], matrixScale[1], matrixScale[2]));
            Components::Transform& t = selectedObject.Get<Components::Transform>();
            if (mCurrentGizmoOperation != ImGuizmo::TRANSLATE)
                t.position = before.position;
            if (mCurrentGizmoOperation != ImGuizmo::ROTATE)
                t.rotation = before.rotation;
            if (mCurrentGizmoOperation != ImGuizmo::SCALE)
                t.scale = before.scale;
        }

    }

    // World-space line drawn always-on-top (ImGui background list), projected with the editor camera;
    // skipped when an end is behind the camera
    void DrawWorldLine(float3 a, float3 b, ImU32 col)
    {
        float4x4 viewProj = mul(inverse(editorState.cameraView), editorState.cameraProj);
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        auto W2S = [&](float3 p, ImVec2& out) -> bool
            {
                float4 cp = mul(float4(p, 1), viewProj); // row-vector convention (engine uses mul(vec, mat))
                float w = (float)cp.w;
                if (w <= 1e-4f) return false;
                float nx = (float)cp.x / w;
                float ny = (float)cp.y / w;
                out = ImVec2((nx * 0.5f + 0.5f) * disp.x, (1.0f - (ny * 0.5f + 0.5f)) * disp.y);
                return true;
            };
        ImVec2 sa, sb;
        if (W2S(a, sa) && W2S(b, sb))
            ImGui::GetBackgroundDrawList()->AddLine(sa, sb, col, 1.5f);
    }

    // Three axis-aligned great circles
    void DrawWireSphere(float3 center, float r, ImU32 col)
    {
        const float twoPi = 6.28318530718f;
        const int seg = 32;
        float3 axes[3][2] = { { float3(1, 0, 0), float3(0, 1, 0) }, { float3(0, 1, 0), float3(0, 0, 1) }, { float3(0, 0, 1), float3(1, 0, 0) } };
        for (auto& ab : axes)
        {
            float3 prev = center + ab[0] * r;
            for (int i = 1; i <= seg; i++)
            {
                float t = twoPi * (float)i / (float)seg;
                float3 cur = center + (ab[0] * cosf(t) + ab[1] * sinf(t)) * r;
                DrawWorldLine(prev, cur, col);
                prev = cur;
            }
        }
    }

    // Selected FogVolume: its sphere, with the same radius scaling as MainView::UpdateFogVolumes
    void DrawFogVolumeGizmo(World::Entity sel)
    {
        auto& v = sel.Get<Components::FogVolume>();
        float4x4 world = ComputeWorldMatrix(sel);
        float scale = std::max(std::max((float)length(world[0].xyz), (float)length(world[1].xyz)), (float)length(world[2].xyz));
        DrawWireSphere(world[3].xyz, v.radius * scale, IM_COL32(120, 200, 255, 255));
    }

    // Wireframe overlay of the selected RenderSettingsVolume's bounds + blend-falloff boundary.
    // Reads the same fields the blend uses (Renderer's UpdateRenderSettings) so the gizmo matches
    // the actual influence region.
    void DrawVolumeGizmo(World::Entity sel)
    {
        auto& v = sel.Get<Components::RenderSettingsVolume>();
        if (v.shape == 2) // Global / unbound: nothing to draw
            return;

        float4x4 world = ComputeWorldMatrix(sel);
        auto Line = [&](float3 a, float3 b, ImU32 col) { DrawWorldLine(a, b, col); };

        const ImU32 boundsCol = IM_COL32(255, 220, 40, 255);
        const ImU32 blendCol = IM_COL32(255, 220, 40, 90);

        float3 center = world[3].xyz;

        if (v.shape == 1) // Sphere: center + radius, ignores rotation/scale (matches the blend)
        {
            DrawWireSphere(center, v.radius, boundsCol);
            if (v.blendDistance > 0.0f)
                DrawWireSphere(center, v.radius + v.blendDistance, blendCol);
        }
        else // Box (shape==0): OBB from the Transform (half along axis i = length(world[i])*0.5)
        {
            float3 ax = world[0].xyz, ay = world[1].xyz, az = world[2].xyz;
            auto box = [&](float3 hx, float3 hy, float3 hz, ImU32 col)
                {
                    float3 corner[8];
                    for (int i = 0; i < 8; i++)
                    {
                        float sx = (i & 1) ? 1.0f : -1.0f;
                        float sy = (i & 2) ? 1.0f : -1.0f;
                        float sz = (i & 4) ? 1.0f : -1.0f;
                        corner[i] = center + hx * sx + hy * sy + hz * sz;
                    }
                    for (int i = 0; i < 8; i++)
                        for (int j = i + 1; j < 8; j++)
                        {
                            int d = i ^ j;
                            if (d == 1 || d == 2 || d == 4) Line(corner[i], corner[j], col); // 12 edges
                        }
                };
            box(ax * 0.5f, ay * 0.5f, az * 0.5f, boundsCol);
            if (v.blendDistance > 0.0f)
            {
                float bd = v.blendDistance;
                box(ax * 0.5f + normalize(ax) * bd, ay * 0.5f + normalize(ay) * bd, az * 0.5f + normalize(az) * bd, blendCol);
            }
        }
    }

    void Update() override final
    {
        ZoneScoped;
        if (editorState.selectedObject.IsValid() && editorState.selectedObject.Has<Components::Transform>())
            EditTransform(editorState.cameraView, editorState.cameraProj, editorState.selectedObject);
        if (editorState.selectedObject.IsValid() && editorState.selectedObject.Has<Components::RenderSettingsVolume>())
            DrawVolumeGizmo(editorState.selectedObject);
        if (editorState.selectedObject.IsValid() && editorState.selectedObject.Has<Components::FogVolume>())
            DrawFogVolumeGizmo(editorState.selectedObject);
    }
};
Guizmo guizmo;

#include <format>
class ECSWindow : public EditorWindow
{
public:
    ECSWindow() : EditorWindow("ECS") {}

    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("ECS", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTable("split", 2, ImGuiTableFlags_Resizable))
        {
            ImGui::TableNextColumn();

            auto& slots = World::instance->entitySlots;
            ImGui::Text(StringFormat("used slots {}", slots.size()).c_str());
            for (uint i = 0; i < slots.size(); i++)
            {
                ImGui::Text(StringFormat("    {} {} {} {}", (size_t)slots[i].permanent, (size_t)slots[i].rev, (size_t)slots[i].pool, (size_t)slots[i].index).c_str());
            }

            auto& freeslots = World::instance->entityFreeSlots;
            ImGui::Text(StringFormat("free slots {}", freeslots.size()).c_str());
            for (uint i = 0; i < freeslots.size(); i++)
            {
                ImGui::Text(StringFormat("    {}", (size_t)freeslots[i]).c_str());
            }

            ImGui::TableNextColumn();

            auto& components = World::instance->components;
            ImGui::Text(StringFormat("components {}", components.size()).c_str());
            for (uint i = 0; i < components.size(); i++)
            {
                String cmps;
                for (uint bitIdx = 1; bitIdx < Components::componentMaxCount; bitIdx++)
                {
                    if (components[i].mask.test(bitIdx))
                    {
                        cmps += Components::names[bitIdx] + " ";
                    }
                }
                ImGui::Text(StringFormat("    {} {}", (size_t)components[i].count, cmps.c_str()).c_str());
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }
};
ECSWindow ecsWindow;

class LightsWindow : public EditorWindow
{
public:
    LightsWindow() : EditorWindow("Lights") {}
    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("Lights", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        // Intensity is in Blender's units, so there is nothing to convert and no
        // unit picker: a sun is irradiance in W/m^2, a point or spot light is
        // radiant power in watts.
        ImGui::TextDisabled("intensity: sun = W/m^2, point/spot = W");

        ImGui::Separator();

        uint queryIndex = World::instance->Query(Components::Light::mask, 0, true);
        auto& result = World::instance->frameQueries[queryIndex];

        if (result.empty())
        {
            ImGui::TextDisabled("No lights in scene.");
            ImGui::End();
            return;
        }

        if (ImGui::BeginTable("lightsTable", 8,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Intensity", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Angle", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Shadow", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableHeadersRow();

            for (uint i = 0; i < (uint)result.size(); i++)
            {
                ImGui::PushID((int)i);
                ImGui::TableNextRow();

                auto& light = result[i].Get<Components::Light>();

                // Name (click to select the entity)
                ImGui::TableSetColumnIndex(0);
                char nameLabel[ECS_NAME_LENGTH + 8];
                if (result[i].Has<Components::Name>())
                    strcpy(nameLabel, result[i].Get<Components::Name>().name);
                else
                    sprintf(nameLabel, "Light %u", i);
                if (ImGui::Selectable(nameLabel, editorState.selectedObject == result[i]))
                    editorState.selectedObject = result[i];

                // Type
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                int typeIdx = (int)light.type;
                const char* typeNames[] = { "Directional", "Spot", "Point" };
                if (ImGui::Combo("##type", &typeIdx, typeNames, 3))
                    light.type = (HLSL::LightType)typeIdx;

                // Color (HDR float RGB)
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1);
                float col[3] = { light.color.x, light.color.y, light.color.z };
                if (ImGui::ColorEdit3("##color", col,
                    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                {
                    light.color.x = col[0];
                    light.color.y = col[1];
                    light.color.z = col[2];
                }

                // Intensity (color.w)
                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##intensity", &light.color.w,
                    0.05f, 0.0f, 1000000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

                // Size
                ImGui::TableSetColumnIndex(4);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##size", &light.size, 0.01f, 0.0f, 1000000.0f);

                // Range
                ImGui::TableSetColumnIndex(5);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##range", &light.range, 0.05f, 0.0f, 1000000.0f);

                // Angle (spot outer cone). Stored in radians, like every angle that
                // reaches the shader; edited in degrees, which is what anyone reading
                // the row expects.
                ImGui::TableSetColumnIndex(6);
                ImGui::SetNextItemWidth(-1);
                float angleDegrees = light.angle * (180.0f / 3.14159265358979f);
                if (ImGui::DragFloat("##angle", &angleDegrees, 0.1f, 0.0f, 180.0f, "%.1f deg"))
                    light.angle = angleDegrees * (3.14159265358979f / 180.0f);

                // Cast shadow
                ImGui::TableSetColumnIndex(7);
                ImGui::Checkbox("##castShadow", &light.castShadow);

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }
};
LightsWindow lightsWindow;

class MainMenu : public EditorWindow
{
public:
    MainMenu() : EditorWindow("MainMenu") { isOpen = true; }

    // Opens the Save World browser, pointed at the loaded world's folder/name; else the project folder + Save.seed.
    static void OpenSaveWorldBrowser()
    {
        namespace fs = std::filesystem;
        fs::path base = editorState.currentWorldPath.empty()
            ? fs::path(Project::instance->projectDir.c_str()) / "Save.seed"
            : fs::path(editorState.currentWorldPath.c_str());
        fileBrowserWindow.OpenSave(base.parent_path().string(), base.filename().string(),
            { ".seed" }, [](String path) // non-capturing -> converts to the callback fn ptr
            {
                if (path.empty()) return; // cancelled / blank name
                World::instance->Save(path);
                editorState.currentWorldPath = path; // remember for the next Save World
            });
    }

    void Update() override final
    {
        ZoneScoped;

        // Ctrl+S: save straight to the loaded world's file; ask for a name if there is none yet.
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && !fileBrowserWindow.isOpen)
        {
            if (editorState.currentWorldPath.empty())
                OpenSaveWorldBrowser();
            else
                World::instance->Save(editorState.currentWorldPath);
        }

        if (ImGui::BeginMenuBar())
        {

            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save Project"))
                {
                    Project::instance->Save();       // write the single project file
                }
                if (ImGui::MenuItem("Save World", "Ctrl+S"))
                {
                    OpenSaveWorldBrowser();
                }
                if (ImGui::MenuItem("Load World"))
                {
                    // Start in the loaded world's folder; else the project folder.
                    namespace fs = std::filesystem;
                    fs::path base = editorState.currentWorldPath.empty()
                        ? fs::path(Project::instance->projectDir.c_str()) / "Save.seed"
                        : fs::path(editorState.currentWorldPath.c_str());
                    fileBrowserWindow.OpenLoad(base.parent_path().string(), { ".seed" }, [](String path)
                        {
                            if (path.empty()) return; // cancelled
                            editorState.selectedObject = {};
                            //World::instance->ClearImmediate();
                            editorState.currentWorldPath = path; // remember for Save/Load World
                            World::instance->Load(path);
                            editorState.dirtyHierarchy = true;
                        });
                }
                if (ImGui::MenuItem("Add Prefab"))
                {
                    // additive: compose the current world with another .seed
                    World::instance->Load("Save.seed");
                }
                if (ImGui::MenuItem("Save Selected As Prefab"))
                {
                    World::Entity sel = editorState.selectedObject;
                    if (sel.IsValid())
                        World::instance->SavePrefab(sel, "Prefab.seed");
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
                        editorState.selectedObject = ent.Make(Components::Entity::mask, "empty");
                        editorState.dirtyHierarchy = true;
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
                        light.type = HLSL::LightType::Directional;
                        light.size = 0.025f;
                        light.castShadow = true;
                        editorState.dirtyHierarchy = true;
                    }
                    if (ImGui::MenuItem("Fog Volume"))
                    {
                        World::Entity ent;
                        editorState.selectedObject = ent.Make(Components::FogVolume::mask | Components::Transform::mask, "Fog Volume");
                        auto& trans = ent.Get<Components::Transform>();
                        trans.position = float3(0);
                        trans.rotation = quaternion::identity();
                        trans.scale = float3(1);
                        auto& volume = ent.Get<Components::FogVolume>();
                        volume.color = float3(1, 1, 1);
                        volume.density = 0.05f;
                        volume.radius = 5.0f;
                        editorState.dirtyHierarchy = true;
                    }
                    if (ImGui::MenuItem("Haze Layer"))
                    {
                        World::Entity ent;
                        editorState.selectedObject = ent.Make(Components::HazeLayer::mask | Components::Transform::mask, "Haze Layer");
                        auto& trans = ent.Get<Components::Transform>();
                        trans.position = float3(0);
                        trans.rotation = quaternion::identity();
                        trans.scale = float3(1);
                        ent.Get<Components::HazeLayer>() = Components::HazeLayer();
                        editorState.dirtyHierarchy = true;
                    }
                    if (ImGui::MenuItem("Prefab"))
                    {
                        World::Entity ent;
                        editorState.selectedObject = ent.Make(Components::Prefab::mask | Components::Transform::mask, "Prefab");
                        auto& trans = ent.Get<Components::Transform>();
                        trans.position = float3(0);
                        trans.rotation = quaternion::identity();
                        trans.scale = float3(1);
                        auto& pf = ent.Get<Components::Prefab>();
                        pf.file[0] = 0;
                        pf.loadDistance = 50.0f;
                        editorState.dirtyHierarchy = true;
                    }
                    if (ImGui::MenuItem("Terrain"))
                    {
                        World::Entity ent;
                        editorState.selectedObject = ent.Make(Components::Terrain::mask | Components::Transform::mask, "Terrain");
                        auto& trans = ent.Get<Components::Transform>();
                        trans.position = float3(0);
                        trans.rotation = quaternion::identity();
                        trans.scale = float3(1);
                        World::Entity matEnt;
                        matEnt.Make(Components::Material::mask | Components::Name::mask, "Terrain Material");
                        auto& mat = matEnt.Get<Components::Material>();
                        for (uint i = 0; i < HLSL::MaterialTextureCount; i++)
                            mat.textures[i] = Components::Handle<Components::Texture>{ entityInvalid };
                        for (uint i = 0; i < HLSL::MaterialParametersCount; i++)
                            mat.parameters[i] = 0.0f;
                        // Routes this material's meshlets to the terrain shading bucket (generic
                        // multi-shader GBuffer draw buckets by Material::shader / shaderIndex, not a
                        // parameter flag). The Shader entity carries its own path, so UpdateMaterials
                        // (Renderer.h) can self-register it in AssetLibrary from saved data alone --
                        // no hardcoded registration needed anywhere for this shader.
                        Components::Handle<Components::Shader> terrainShader;
                        auto& terrainShaderCmp = terrainShader.GetPermanent();
                        terrainShaderCmp.id = AssetLibrary::instance->Add("src\\Shaders\\terrainmesh.hlsl|DefaultGTerrain", "src\\Shaders\\terrainmesh.hlsl|DefaultGTerrain");
                        strcpy_s(terrainShaderCmp.path, ECS_SHADER_PATH, "src\\Shaders\\terrainmesh.hlsl|DefaultGTerrain");
                        mat.shader = terrainShader;

                        auto& terrain = ent.Get<Components::Terrain>();
                        terrain.heightmap = Components::Handle<Components::Texture>{ entityInvalid };
                        terrain.material = Components::Handle<Components::Material>{ matEnt };
                        terrain.useProceduralHeightmap = 1;
                        terrain.noiseScale = 1.0f;
                        terrain.noiseOctaves = 2;
                        terrain.noiseLacunarity = 2.412f;
                        terrain.noiseGain = 1.0f;
                        terrain.noiseSeed = 1;
                        terrain.heightScale = 1000.0f;
                        terrain.heightOffset = 0.0f;
                        terrain.heightmapBlurRadius = 0;
                        terrain.worldExtent = 8192.0f;
                        terrain.quadtreeDepth = 10;
                        terrain.targetScreenSize = 0.15f;
                        terrain.gridSpacing = 0.66f;
                        terrain.gridSize = 16.0f;
                        terrain.erosionEnabled = 2;
                        terrain.erosionOctaves = 3;
                        terrain.erosionScale = 0.16f;
                        terrain.erosionStrength = 0.1f;
                        terrain.erosionGullyWeight = 3.3f;
                        terrain.erosionDetail = 1.0f;
                        terrain.erosionLacunarity = 2.476f;
                        terrain.erosionGain = 0.67f;
                        terrain.erosionCellScale = 0.64f;
                        terrain.erosionNormalization = 0.83f;
                        terrain.erosionRidgeRounding = 0.13f;
                        terrain.erosionCreaseRounding = 0.81f;
                        terrain.BVHMaxDepth = 5;
                        terrain.BVHMinRadius = 512.0f;
                        terrain.BVHMaxRadius = 4096.0f;
                        if (MeshLoader::instance != nullptr)
                            terrain.gridMesh = MeshLoader::instance->BuildTerrainGridMesh(terrain.gridSpacing, terrain.gridSize);
                        editorState.dirtyHierarchy = true;
                    }
                    if (ImGui::MenuItem("Vehicle"))
                    {
                        MakeDefaultVehicle();
                    }
                    ImGui::SetItemTooltip("A drivable 4-wheel car: a dynamic box chassis plus four child Wheel entities.\nIt renders nothing on its own -- turn on drawPhysicsShapes / drawPhysicsConstraints to see it,\nand drive it from the Vehicle component's forwardInput / rightInput while playing.");
                    if (ImGui::MenuItem("Buggy (1/8 scale)"))
                    {
                        MakeDefaultBuggy();
                    }
                    ImGui::SetItemTooltip("The same thing at 1/8 RC scale: a 400 x 140 x 90 mm body, 327 mm wheelbase, 102 mm\ntires narrow at the front and fat at the rear, 3 kg. Every number that does not scale with the\nvehicle is re-tuned for it -- suspension frequency above all, since static sag is g / (2*pi*f)^2\nno matter how small the car gets.");
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Generate components Metadata"))
                {
                    // Rewrites ComponentMetaData.h from every header of the engine source folder.
                    // Takes effect on the next build; watch the log for skipped fields.
                    GenerateComponentMetaData();
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

            ImGuiStyle& style = ImGui::GetStyle();
            float width = 0.0f;
            width += ImGui::CalcTextSize("Stop").x;
            width += style.ItemSpacing.x;
            width += ImGui::CalcTextSize("average %.3f ms/frame (%.1f FPS)").x;
            AlignForWidth(width);

            if (ImGui::MenuItem(engineState.playing ? "Stop" : "Play") || IOs::instance->keys.pressed[VK_F2])
            {
                engineState.playing = !engineState.playing;
            }
            ImGui::Separator();

            //duplicate of profiler framerate info
            ImGui::Text("average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

            ImGui::Separator();


            ImGui::EndMainMenuBar();
        }
    }
};
MainMenu mainMenu;

void UIHelpers::DrawHandle(EntityBase& target, Components::Mask dataTemplateType)
{
    World::Entity& handleTarget = *(World::Entity*)&target;
    String handleName = "--";
    if (handleTarget != entityInvalid)
    {
        if (handleTarget.Has<Components::Name>())
            handleName = handleTarget.Get<Components::Name>().name;

        if (ImGui::SmallButton("->"))
        {
            editorState.selectedObject = handleTarget;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            handleTarget = entityInvalid;
        }
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("o"))
    {
        handlePickingWindow.Open(dataTemplateType, &handleTarget);
    }
    ImGui::SameLine();
    // Texture handles can additionally import a source file (e.g. a heightmap PNG) straight
    // from the project directory, instead of only picking among already-imported textures.
    if (dataTemplateType == Components::Texture::mask)
    {
        if (ImGui::SmallButton("Import..."))
        {
            pendingTextureImportTarget = &handleTarget;
            fileBrowserWindow.OpenLoad(AssetLibrary::instance->importPath,
                { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".tif" },
                [](String path)
                {
                    if (path.empty() || pendingTextureImportTarget == nullptr)
                        return;
                    std::filesystem::path p(path.c_str());
                    Components::Handle<Components::Texture> tex = TextureLoader::instance->ImportByName(String(p.filename().string()));
                    *pendingTextureImportTarget = World::Entity(tex);
                    pendingTextureImportTarget = nullptr;
                });
        }
        ImGui::SameLine();
    }
    ImGui::Text(handleName.c_str());
}

/*
ImGui::ShowDemoWindow(&openDemo);
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
