#pragma once
namespace Systems
{
    // Frustum + screen-space-size CPU quadtree (see Components::Terrain / plan step 4): each
    // frame, re-derive every Terrain's leaf set by walking the quadtree, reusing last frame's
    // instances as a pool in emission order -- a reused entity is only rewritten (transform +
    // bounding sphere, flagged State::dirty for the renderer's UpdateInstances re-upload) when
    // its node actually changed, extra nodes Make() new entities, leftover instances are
    // released after the walk (see EmitNodeInstance). The walk itself goes level by level:
    // cull nodes entirely outside the main camera's frustum (a culled
    // node's children can't be more visible than its own bounding volume, so they're dropped
    // without being subdivided), and subdivide visible nodes while their estimated on-screen
    // footprint exceeds Terrain::targetScreenSize (assuming a node's AABB height == heightScale,
    // per the current milestone's simplification) and the max quadtree depth hasn't been reached;
    // otherwise emit a leaf instance of the shared grid mesh scaled to that node's footprint
    // (coarser nodes reuse the same vertex mesh at a larger world-space scale -- the standard
    // clipmap/quadtree LOD trick, see plan). NOT registered in World::systems (those run in
    // parallel): it creates/destroys entities, so it must run single-threaded -- call Update()
    // once per frame from the main loop, same slot as PrefabStreaming (after DeferredRelease,
    // before the ECS systems and the editor read the world).
    struct TerrainStreaming : SystemBase
    {
        struct Node
        {
            float2 center;
            float size;
            uint depth;
        };

        // CPU-only min/max height pyramid, quadtree-cell-aligned to a Terrain's own subdivision (level d
        // is a (1<<d) x (1<<d) grid of {min,max} cells covering the full heightmap UV square). Values are
        // the heightmap's RAW sampled red-channel value (matching terrainmesh.hlsl's .x read), BEFORE
        // heightScale/heightOffset -- those are per-Terrain and applied at lookup time (see
        // Systems::TerrainStreaming::ComputeNodeBoundingSphere). Not cached/shared across Terrains: built
        // synchronously and directly by Systems::TerrainStreaming whenever a Terrain's heightmap handle
        // changes, and owned by that system (see TerrainStreaming::heightPyramids) rather than living on
        // this (or any) ECS component, since the ECS pool allocates component storage with raw malloc and
        // migrates it via memcpy/memset (World::Pool::On, EntitySlot::Copy) -- unsafe for a
        // std::vector-owning struct.
        struct TerrainHeightPyramid
        {
            // Default-constructs as the conservative depth-0 full-[0,1]-range pyramid, so a
            // not-yet-built (or never-built, e.g. no heightmap assigned) instance is always safe to
            // QueryRange() without a separate null/"not ready" check.
            uint depthBuilt = 0;
            std::vector<std::vector<float>> levelMin = { { 0.0f } }; // level d: row-major [z*(1<<d)+x]
            std::vector<std::vector<float>> levelMax = { { 1.0f } };

            // Conservative union of whichever cells the UV rect overlaps at min(desiredDepth, depthBuilt):
            // a node whose true depth exceeds depthBuilt is bounded by the coarsest/deepest level actually
            // built, which is itself a valid (just looser) bound by construction (bottom-up min/max
            // reduction).
            void QueryRange(float2 uvMin, float2 uvMax, uint desiredDepth, float& outMin, float& outMax) const
            {
                uint level = std::min(desiredDepth, depthBuilt);
                uint dim = 1u << level;

                float minX = std::clamp((float)uvMin.x, 0.0f, 1.0f), maxX = std::clamp((float)uvMax.x, 0.0f, 1.0f);
                float minZ = std::clamp((float)uvMin.y, 0.0f, 1.0f), maxZ = std::clamp((float)uvMax.y, 0.0f, 1.0f);
                if (maxX < minX) maxX = minX;
                if (maxZ < minZ) maxZ = minZ;

                int x0 = std::clamp((int)std::floor(minX * dim), 0, (int)dim - 1);
                int x1 = std::clamp((int)std::floor(maxX * dim - 1e-5f), 0, (int)dim - 1);
                int z0 = std::clamp((int)std::floor(minZ * dim), 0, (int)dim - 1);
                int z1 = std::clamp((int)std::floor(maxZ * dim - 1e-5f), 0, (int)dim - 1);

                const auto& mn = levelMin[level];
                const auto& mx = levelMax[level];
                outMin = FLT_MAX; outMax = -FLT_MAX;
                for (int z = z0; z <= z1; z++)
                    for (int x = x0; x <= x1; x++)
                    {
                        outMin = std::min(outMin, mn[(size_t)z * dim + x]);
                        outMax = std::max(outMax, mx[(size_t)z * dim + x]);
                    }
            }
        };

        std::unordered_map<uint, std::vector<EntityBase>> instancesPerTerrain; // key = Terrain entity id

        // Per-terrain min/max height pyramid bookkeeping, sibling to instancesPerTerrain above (same
        // key, same lifetime) -- kept outside the ECS component system since it owns std::vectors and
        // the ECS pool is raw-malloc/memcpy-based (see Components::TerrainHeightPyramid). Rebuilt
        // synchronously, immediately, whenever a Terrain's heightmap handle changes (see Update()).
        struct TerrainHeightData
        {
            Components::Handle<Components::Texture> builtForHeightmap; // invalid = not built yet
            TerrainHeightPyramid pyramid; // defaults to the conservative [0,1] pyramid
        };
        std::unordered_map<uint, TerrainHeightData> heightPyramids; // key = Terrain entity id

        // Persistent scratch buffers reused across frames (clear() keeps capacity) so the
        // steady-state per-frame quadtree walk performs zero heap allocations: nodeScratch[0]/[1]
        // ping-pong as the current/next subdivision level in Update(), presentTerrains replaces a
        // per-frame unordered_set (a handful of ids at most, a linear scan beats hashing anyway).
        std::vector<Node> nodeScratch[2];
        std::vector<uint> presentTerrains;

        void On() override {}
        void Off() override { ClearAll(); }

        void ClearAll()
        {
            ZoneScoped;
            for (auto& kv : instancesPerTerrain)
                for (auto& eb : kv.second)
                {
                    World::Entity e = eb;
                    if (e.IsValid()) e.ReleaseImmediately();
                }
            instancesPerTerrain.clear();
            heightPyramids.clear();
        }

        // Builds a CPU-only min/max height pyramid for a terrain heightmap: resolves the heightmap's
        // original source file (same Name -> FindInImportPath -> decode pattern as OMMBaker::RequestBake)
        // and reduces it bottom-up into Components::TerrainHeightPyramid levels. Called synchronously,
        // directly, by Systems::TerrainStreaming::Update whenever a Terrain's heightmap handle changes
        // (see World.h Components::buildTerrainHeightPyramid) -- always returns a usable pyramid,
        // degrading to a conservative full-[0,1]-range depth-0 pyramid on any failure.
        TerrainHeightPyramid BuildTerrainHeightPyramid(Components::Handle<Components::Texture> heightmap)
        {
            ZoneScoped;
            static constexpr uint MaxDepth = 10; // finest level = 1024x1024 cells, ~10-11MB worst case total

            // Components::TerrainHeightPyramid default-constructs as the conservative depth-0
            // full-[0,1]-range pyramid, so every failure path below can just return {}.
            auto conservativeFallback = []() { return TerrainHeightPyramid{}; };

            if (!heightmap.IsValid())
                return conservativeFallback();

            World::Entity ent = World::Entity(heightmap);
            String texName = ent.Get<Components::Name>().name;
            String srcPath = AssetLibrary::instance->FindInImportPath(texName);
            if (srcPath.size() == 0)
            {
                IOs::Log("terrain height pyramid: source image not found for '{}', using conservative fallback", texName.c_str());
                return conservativeFallback();
            }

            DirectX::ScratchImage image;
            if (!LoadImageFromDisk(srcPath, image))
            {
                IOs::Log("terrain height pyramid: failed to decode '{}', using conservative fallback", srcPath.c_str());
                return conservativeFallback();
            }

            // Normalize to full-precision RGBA regardless of source format/bit-depth: heightScale/
            // heightOffset amplify quantization error, so keep float precision rather than truncating.
            DirectX::ScratchImage converted;
            const DirectX::Image* img = image.GetImage(0, 0, 0);
            if (img == nullptr || img->width == 0 || img->height == 0)
                return conservativeFallback();
            if (img->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
            {
                if (FAILED(DirectX::Convert(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
                    DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_DEFAULT,
                    DirectX::TEX_THRESHOLD_DEFAULT, converted)))
                {
                    IOs::Log("terrain height pyramid: failed to convert '{}', using conservative fallback", srcPath.c_str());
                    return conservativeFallback();
                }
                img = converted.GetImage(0, 0, 0);
            }

            uint texW = (uint)img->width, texH = (uint)img->height;
            uint maxDepth = MaxDepth;
            while (maxDepth > 0 && (1u << maxDepth) > std::max(texW, texH))
                maxDepth--;

            TerrainHeightPyramid pyramid;
            pyramid.depthBuilt = maxDepth;
            pyramid.levelMin.resize(maxDepth + 1);
            pyramid.levelMax.resize(maxDepth + 1);

            // ---- finest level: reduce real texels (red channel, matching terrainmesh.hlsl's .x read) ----
            uint dim = 1u << maxDepth;
            auto& fMin = pyramid.levelMin[maxDepth];
            auto& fMax = pyramid.levelMax[maxDepth];
            fMin.assign((size_t)dim * dim, FLT_MAX);
            fMax.assign((size_t)dim * dim, -FLT_MAX);

            for (uint py = 0; py < texH; py++)
            {
                const float* row = (const float*)(img->pixels + (size_t)py * img->rowPitch);
                uint cz = std::min((uint)((uint64_t)py * dim / texH), dim - 1);
                for (uint px = 0; px < texW; px++)
                {
                    uint cx = std::min((uint)((uint64_t)px * dim / texW), dim - 1);
                    float v = row[(size_t)px * 4 + 0]; // red channel
                    size_t idx = (size_t)cz * dim + cx;
                    fMin[idx] = std::min(fMin[idx], v);
                    fMax[idx] = std::max(fMax[idx], v);
                }
            }

            // ---- coarser levels: each parent cell = min/max of its 4 children ----
            for (int d = (int)maxDepth - 1; d >= 0; d--)
            {
                uint pdim = 1u << d, cdim = pdim * 2;
                auto& pMin = pyramid.levelMin[d]; auto& pMax = pyramid.levelMax[d];
                auto& cMin = pyramid.levelMin[d + 1]; auto& cMax = pyramid.levelMax[d + 1];
                pMin.assign((size_t)pdim * pdim, FLT_MAX);
                pMax.assign((size_t)pdim * pdim, -FLT_MAX);
                for (uint z = 0; z < pdim; z++)
                    for (uint x = 0; x < pdim; x++)
                    {
                        float mn = FLT_MAX, mx = -FLT_MAX;
                        for (uint dz = 0; dz < 2; dz++)
                            for (uint dx = 0; dx < 2; dx++)
                            {
                                size_t cidx = (size_t)(z * 2 + dz) * cdim + (x * 2 + dx);
                                mn = std::min(mn, cMin[cidx]);
                                mx = std::max(mx, cMax[cidx]);
                            }
                        pMin[(size_t)z * pdim + x] = mn;
                        pMax[(size_t)z * pdim + x] = mx;
                    }
            }
            return pyramid;
        }

        // Mirrors common.hlsl's FrustumCulling(camera, boundingSphere) exactly (same plane
        // convention, same near-camera-sphere-intersection guard) so CPU-side node culling agrees
        // with the GPU meshlet culling that eventually draws the surviving instances.
        bool FrustumCulled(const HLSL::Camera& camera, float4 boundingSphere)
        {
            float radius = (float)boundingSphere.w;
            bool culled = false;
            for (uint i = 0; i < 6; i++)
            {
                float d = (float)(dot(camera.planes[i].xyz, boundingSphere.xyz) + camera.planes[i].w);
                culled = culled || (d < -radius);
            }
            float distToCam = (float)length(camera.worldPos.xyz - boundingSphere.xyz);
            culled = culled && (distToCam > radius);
            return culled;
        }

        // Builds a TRUE 3D world-space bounding sphere for a quadtree node: the existing XZ
        // half-diagonal term combined via Pythagoras with the node's real height half-extent, looked
        // up from the heightmap's min/max pyramid. pyramid is always safe to query (defaults to the
        // conservative full-[0,1]-range pyramid when no heightmap is assigned yet, see
        // Components::TerrainHeightPyramid / Update() below).
        float4 ComputeNodeBoundingSphere(Components::Terrain& terrain, float terrainY, const TerrainHeightPyramid& pyramid, const Node& node)
        {
            float worldExtent = std::max(terrain.worldExtent, 1e-5f);
            // Matches terrainmesh.hlsl's UV formula exactly (uv = worldPosXZ.xz / terrainWorldSize +
            // 0.5, using ABSOLUTE world position) -- node.center is already absolute (root pushed as
            // float2(origin.x, origin.z) in Update()), so this samples the same heightmap region the
            // mesh shader will displace.
            float2 uvCenter = node.center / worldExtent + float2(0.5f, 0.5f);
            float uvHalf = (node.size / worldExtent) * 0.5f;

            float minRaw, maxRaw;
            pyramid.QueryRange(uvCenter - float2(uvHalf, uvHalf), uvCenter + float2(uvHalf, uvHalf), node.depth, minRaw, maxRaw);

            // The pyramid bounds the RAW heightmap, but the mesh shader displaces with the
            // GPU-eroded map (TerrainErosion pass), so pad by the filter's hard bound so
            // eroded-but-still-visible nodes are never culled 
            if (terrain.erosionEnabled != 0)
            {
                float gain = terrain.erosionGain;
                float octaveSum = std::abs(gain - 1.0f) < 1e-4f
                    ? (float)terrain.erosionOctaves
                    : (1.0f - std::pow(gain, (float)terrain.erosionOctaves)) / (1.0f - gain);
                float maxDelta = terrain.erosionStrength * terrain.erosionScale * octaveSum * std::max(1.0f, terrain.erosionGullyWeight);
                minRaw = std::max(0.0f, minRaw - maxDelta);
                maxRaw = std::min(1.0f, maxRaw + maxDelta);
            }

            float y0 = terrainY + minRaw * terrain.heightScale + terrain.heightOffset;
            float y1 = terrainY + maxRaw * terrain.heightScale + terrain.heightOffset;
            float minY = std::min(y0, y1), maxY = std::max(y0, y1); // heightScale may be negative

            float3 sphereCenter(node.center.x, (minY + maxY) * 0.5f, node.center.y);
            float halfDiag = node.size * 0.70710678f;   // half-diagonal of a size x size square
            float halfHeight = (maxY - minY) * 0.5f;
            float radius = length(float2(halfDiag, halfHeight)); // Pythagorean combination
            return float4(sphereCenter, radius);
        }

        // Returns the node's estimated on-screen size as a fraction of the viewport's vertical
        // extent (resolution-independent), or a negative value if the node is entirely outside the
        // frustum.
        float CullNodeAndGetNDCSize(const HLSL::Camera& camera, float invTanHalfFovY, const Node& node, float4 boundingSphere)
        {
            if (FrustumCulled(camera, boundingSphere))
                return -1.0f;

            float dist = std::max((float)length(boundingSphere.xyz - camera.worldPos.xyz), 0.001f);
            return (node.size / dist) * 0.5f * invTanHalfFovY;
        }

        void Subdivide(const Node& node, std::vector<Node>& out)
        {
            float quarter = node.size * 0.25f;
            float half = node.size * 0.5f;
            static const float2 dirs[4] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };
            for (uint i = 0; i < 4; i++)
                out.push_back({ node.center + dirs[i] * quarter, half, node.depth + 1 });
        }

        EntityBase MakeNodeInstance(Components::Terrain& terrain, float3 position, float scaleXZ, float4 boundingSphere)
        {
            ZoneScoped;
            World::Entity ent;
            ent.Make(Components::Transform::mask | Components::WorldMatrix::mask | Components::Instance::mask);
            auto& tr = ent.Get<Components::Transform>();
            tr.position = position;
            tr.rotation = quaternion::identity();
            tr.scale = float3(scaleXZ, 1.0f, scaleXZ);
            auto& inst = ent.Get<Components::Instance>();
            inst.mesh = terrain.gridMesh;
            inst.material = terrain.material;
            inst.meshRT = Components::Handle<Components::Mesh>{ entityInvalid };
            inst.boundingSphereOverride = boundingSphere; // world-space, real per-node height range
            ent.Get<Components::State>().flags |= Components::State::Flags::transient;
            return ent;
        }

        // Emits the leaf instance for a quadtree node by claiming pool[usedCount] (last frame's
        // instances, in emission order -- the walk is deterministic, so a static camera re-emits
        // identical nodes in identical order and every slot matches its previous content): a
        // reused entity is only rewritten (transform + bounding sphere) and flagged State::dirty
        // -- picked up by the renderer's UpdateInstances, which clears the flag once consumed --
        // when the node actually changed, otherwise it's left untouched and the renderer skips it
        // entirely (loaded && !dirty). Only when this frame emits more leaves than last frame are
        // new entities Make()'d; leftovers are released after the walk (see Update()). A mesh/
        // material swap releases + remakes the entity instead of mutating it in place:
        // UpdateInstances accounts per-shader-bucket meshlet contributions on first load only
        // (documented limitation there).
        void EmitNodeInstance(Components::Terrain& terrain, float terrainY, const Node& node, float4 boundingSphere, std::vector<EntityBase>& pool, uint& usedCount)
        {
            if (!terrain.gridMesh.IsValid() || !terrain.material.IsValid())
                return;

            float gridSize = terrain.gridSize > 0.001f ? terrain.gridSize : 0.001f;
            float3 position = float3(node.center.x, terrainY, node.center.y);
            float scaleXZ = node.size / gridSize;

            if (usedCount < pool.size())
            {
                World::Entity ent = pool[usedCount];
                if (ent.IsValid())
                {
                    auto& inst = ent.Get<Components::Instance>();
                    if (inst.mesh == terrain.gridMesh && inst.material == terrain.material)
                    {
                        auto& tr = ent.Get<Components::Transform>();
                        bool changed =
                            (float)tr.position.x != (float)position.x ||
                            (float)tr.position.y != (float)position.y ||
                            (float)tr.position.z != (float)position.z ||
                            (float)tr.scale.x != scaleXZ ||
                            (float)inst.boundingSphereOverride.x != (float)boundingSphere.x ||
                            (float)inst.boundingSphereOverride.y != (float)boundingSphere.y ||
                            (float)inst.boundingSphereOverride.z != (float)boundingSphere.z ||
                            (float)inst.boundingSphereOverride.w != (float)boundingSphere.w;
                        if (changed)
                        {
                            tr.position = position;
                            tr.scale = float3(scaleXZ, 1.0f, scaleXZ);
                            inst.boundingSphereOverride = boundingSphere;
                            ent.Get<Components::State>().flags |= Components::State::Flags::dirty;
                        }
                        usedCount++;
                        return;
                    }
                    ent.ReleaseImmediately(); // mesh/material swapped: retire, remake fresh below
                }
                pool[usedCount++] = MakeNodeInstance(terrain, position, scaleXZ, boundingSphere);
                return;
            }
            pool.push_back(MakeNodeInstance(terrain, position, scaleXZ, boundingSphere));
            usedCount++;
        }

        // Processes one quadtree level: nodes still too big get subdivided into newNodesToSubdivide
        // (fed back in as the next level by the caller's loop), nodes that are the right size or
        // hit the depth cap become leaf instances, culled nodes are dropped entirely. Computes each
        // node's bounding sphere once and reuses it for both the cull/size test and the leaf instance.
        void CullQuadtree(Components::Terrain& terrain, float terrainY, const TerrainHeightPyramid& pyramid,
            const HLSL::Camera& camera, float invTanHalfFovY,
            std::vector<Node>& nodesToSubdivide, std::vector<Node>& newNodesToSubdivide,
            std::vector<EntityBase>& pool, uint& usedCount)
        {
            ZoneScoped;
            for (auto& node : nodesToSubdivide)
            {
                float4 boundingSphere = ComputeNodeBoundingSphere(terrain, terrainY, pyramid, node);
                float size = CullNodeAndGetNDCSize(camera, invTanHalfFovY, node, boundingSphere);
                if (size < 0.0f)
                    continue; // frustum-culled: nothing under this node can be visible either

                if (size > terrain.targetScreenSize && node.depth < terrain.quadtreeDepth)
                    Subdivide(node, newNodesToSubdivide);
                else
                    EmitNodeInstance(terrain, terrainY, node, boundingSphere, pool, usedCount);
            }
        }

        void Update(World* world) override
        {
            ZoneScoped;

            uint terrainQueryIndex = world->Query(Components::Terrain::mask, 0, true);
            auto& terrains = world->frameQueries[terrainQueryIndex];
            if (terrains.empty()) { ClearAll(); return; }

            // Bridged from Renderer.h (UpdateCameras) -- not yet available during the first frame
            // or two before the renderer finishes initializing.
            if (Components::getMainCamera == nullptr)
                return;
            HLSL::Camera camera = Components::getMainCamera();
            float invTanHalfFovY = 1.0f / tanf(camera.fovY * (3.14159265f / 180.0f) * 0.5f);

            presentTerrains.clear();
            for (auto& eb : terrains)
            {
                ZoneScopedN("UpdateTerrain");
                World::Entity e = eb;
                if (!e.IsValid()) continue;
                presentTerrains.push_back(e.id);

                auto& terrain = e.Get<Components::Terrain>();

                // Ride the heightmap + decode params on the shared material so terrainmesh.hlsl can
                // read them. Shading-bucket routing is via the material's shader (already set to
                // DefaultGTerrain at Terrain creation, see UI.h), not a parameter flag.
                if (terrain.material.IsValid())
                {
                    auto& mat = terrain.material.Get();
                    mat.textures[Components::TerrainHeightmapTextureSlot] = terrain.heightmap;
                    mat.parameters[Components::TerrainHeightScaleParam] = terrain.heightScale;
                    mat.parameters[Components::TerrainHeightOffsetParam] = terrain.heightOffset;
                    mat.parameters[Components::TerrainWorldSizeParam] = terrain.worldExtent;
                    // params TerrainErodedHeightmapParam (7) / TerrainErosionDiffParam (9) are
                    // owned and written by the TerrainErosion pass (Renderer.h), not here.
                }

                // Rebuild the height pyramid synchronously, immediately, only when the heightmap
                // handle actually changed (cheap handle-equality check otherwise) -- guaranteed ready
                // for this SAME frame's quadtree walk below, no "pending" state to handle. A cleared
                // heightmap resets to the default conservative pyramid rather than keeping stale
                // data -- but only ONCE (reconstructing TerrainHeightData allocates its default
                // pyramid vectors, so don't redo it every frame the heightmap stays unassigned).
                auto& heightData = heightPyramids[e.id];
                if (!terrain.heightmap.IsValid())
                {
                    if (heightData.builtForHeightmap.IsValid())
                        heightData = {};
                }
                else if (heightData.builtForHeightmap != terrain.heightmap)
                {
                    heightData.builtForHeightmap = terrain.heightmap;
                    heightData.pyramid = BuildTerrainHeightPyramid(terrain.heightmap);
                }

                float3 origin = e.Has<Components::Transform>() ? e.Get<Components::Transform>().position : float3(0);

                // last frame's leaves double as this frame's reuse pool: the walk below claims
                // pool[0..usedCount) in emission order (see EmitNodeInstance), leftovers past
                // usedCount found no node this frame and are released after the walk.
                auto& pool = instancesPerTerrain[e.id];
                uint usedCount = 0;

                auto& nodesToSubdivide = nodeScratch[0];
                auto& newNodesToSubdivide = nodeScratch[1];
                nodesToSubdivide.clear();
                nodesToSubdivide.push_back({ float2(origin.x, origin.z), terrain.worldExtent, 0 });
                while (!nodesToSubdivide.empty())
                {
                    newNodesToSubdivide.clear();
                    CullQuadtree(terrain, origin.y, heightData.pyramid, camera, invTanHalfFovY, nodesToSubdivide, newNodesToSubdivide, pool, usedCount);
                    std::swap(nodesToSubdivide, newNodesToSubdivide); // pointer swap, both keep capacity
                }

                {
                    ZoneScopedN("ReleaseLeftoverInstances");
                    for (size_t i = usedCount; i < pool.size(); i++)
                    {
                        World::Entity c = pool[i];
                        if (c.IsValid()) c.ReleaseImmediately();
                    }
                    pool.resize(usedCount);
                }
            }

            // drop bookkeeping (and release orphaned instances) for terrains removed since last frame
            auto stillPresent = [&](uint id) { return std::find(presentTerrains.begin(), presentTerrains.end(), id) != presentTerrains.end(); };
            for (auto it = instancesPerTerrain.begin(); it != instancesPerTerrain.end();)
            {
                if (!stillPresent(it->first))
                {
                    for (auto& eb : it->second) { World::Entity c = eb; if (c.IsValid()) c.ReleaseImmediately(); }
                    it = instancesPerTerrain.erase(it);
                }
                else ++it;
            }
            for (auto it = heightPyramids.begin(); it != heightPyramids.end();)
            {
                if (!stillPresent(it->first)) it = heightPyramids.erase(it);
                else ++it;
            }

            //world->structureChanged = true;
        }
    };
}