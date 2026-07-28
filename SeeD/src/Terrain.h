#pragma once
namespace Systems
{
    struct TerrainStreaming : SystemBase
    {
        struct Node
        {
            float2 center;
            float size;
            uint depth;
            uint2 coord;
        };

        static uint64_t NodeKey(const Node& node)
        {
            seedAssert(node.coord.x < (1u << 28) && node.coord.y < (1u << 28));
            return ((uint64_t)node.depth << 56) | ((uint64_t)node.coord.x << 28) | (uint64_t)node.coord.y;
        }

        struct PooledNodeInstance
        {
            EntityBase entity;
            uint32_t lastTouchedFrame = 0;
        };

        struct TerrainHeightPyramid
        {
            uint depthBuilt = 0;
            std::vector<std::vector<float>> levelMin = { { 0.0f } }; // level d: row-major [z*(1<<d)+x]
            std::vector<std::vector<float>> levelMax = { { 1.0f } };

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

        std::unordered_map<uint, std::unordered_map<uint64_t, PooledNodeInstance>> instancesPerTerrain;
        uint32_t frameCounter = 0;

        struct TerrainHeightData
        {
            Components::Handle<Components::Texture> builtForHeightmap;
            TerrainHeightPyramid pyramid;
        };
        std::unordered_map<uint, TerrainHeightData> heightPyramids;

        std::vector<Node> nodeScratch[2];
        std::vector<uint> presentTerrains;

        struct ChurnCounters
        {
            uint reusedUnchanged = 0; // pool slot kept as-is, nothing written
            uint reusedRewritten = 0; // pool slot kept, transform/sphere rewritten (State::dirty)
            uint created = 0;         // entity Make()'d (incl. the retire+remake mesh/material swap)
            uint released = 0;        // entity released (leftovers, swaps, terrain removal, ClearAll)
        };
        ChurnCounters churn;

        void PublishChurn()
        {
            if (Profiler::instance == nullptr)
                return;
            Profiler::instance->frameData.terrainReusedUnchanged = churn.reusedUnchanged;
            Profiler::instance->frameData.terrainReusedRewritten = churn.reusedRewritten;
            Profiler::instance->frameData.terrainCreated = churn.created;
            Profiler::instance->frameData.terrainReleased = churn.released;
        }

        void On() override {}
        void Off() override { ClearAll(); }

        void ClearAll()
        {
            ZoneScoped;
            for (auto& kv : instancesPerTerrain)
                for (auto& nodeKv : kv.second)
                    ReleaseNodeEntity(nodeKv.second.entity);
            instancesPerTerrain.clear();
            heightPyramids.clear();
        }

        TerrainHeightPyramid BuildTerrainHeightPyramid(Components::Handle<Components::Texture> heightmap)
        {
            ZoneScoped;
            static constexpr uint MaxDepth = 10;

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

        // Max raw [0,1] height delta the erosion filter can add (terrainErosion.hlsl clamps
        // gullies to +/-1 before scaling by strength*scale*gain^octave), summed geometrically
        // across octaves. Shared by ComputeNodeBoundingSphere (culling pad) and
        // ComputeOverrideHeightRange (override mesh AABB) so the two never drift apart.
        static float ComputeErosionRawPad(Components::Terrain& terrain)
        {
            if (terrain.erosionEnabled == 0)
                return 0.0f;
            float gain = terrain.erosionGain;
            float octaveSum = std::abs(gain - 1.0f) < 1e-4f
                ? (float)terrain.erosionOctaves
                : (1.0f - std::pow(gain, (float)terrain.erosionOctaves)) / (1.0f - gain);
            return terrain.erosionStrength * terrain.erosionScale * octaveSum * std::max(1.0f, terrain.erosionGullyWeight);
        }

        // OBJECT-SPACE (no terrainY) Y range a node's override mesh will ever displace into --
        // the encode domain for its SNORM16 position quantization (MeshStorage::CreateMeshOverride).
        // Conservative/per-terrain, not per-node-footprint (unlike the culling pyramid query):
        // 16-bit precision over the whole padded range is already sub-mm, so there is no need for
        // the tighter per-node bound culling uses.
        static void ComputeOverrideHeightRange(Components::Terrain& terrain, float& outMinY, float& outExtentY)
        {
            float maxDelta = ComputeErosionRawPad(terrain);
            float minRaw = std::max(0.0f, 0.0f - maxDelta);
            float maxRaw = std::min(1.0f, 1.0f + maxDelta);
            float y0 = minRaw * terrain.heightScale + terrain.heightOffset;
            float y1 = maxRaw * terrain.heightScale + terrain.heightOffset;
            outMinY = std::min(y0, y1);
            outExtentY = std::abs(y1 - y0);
        }

        float4 ComputeNodeBoundingSphere(Components::Terrain& terrain, float terrainY, const TerrainHeightPyramid& pyramid, const Node& node)
        {
            float worldExtent = std::max(terrain.worldExtent, 1e-5f);
            float2 uvCenter = node.center / worldExtent + float2(0.5f, 0.5f);
            float uvHalf = (node.size / worldExtent) * 0.5f;

            float minRaw, maxRaw;
            pyramid.QueryRange(uvCenter - float2(uvHalf, uvHalf), uvCenter + float2(uvHalf, uvHalf), node.depth, minRaw, maxRaw);

            if (terrain.erosionEnabled != 0)
            {
                float maxDelta = ComputeErosionRawPad(terrain);
                minRaw = std::max(0.0f, minRaw - maxDelta);
                maxRaw = std::min(1.0f, maxRaw + maxDelta);
            }

            float y0 = terrainY + minRaw * terrain.heightScale + terrain.heightOffset;
            float y1 = terrainY + maxRaw * terrain.heightScale + terrain.heightOffset;
            float minY = std::min(y0, y1), maxY = std::max(y0, y1);

            float3 sphereCenter(node.center.x, (minY + maxY) * 0.5f, node.center.y);
            float halfDiag = node.size * 0.70710678f;
            float halfHeight = (maxY - minY) * 0.5f;
            float radius = length(float2(halfDiag, halfHeight));
            return float4(sphereCenter, radius);
        }

        // Frustum visibility is checked separately by the caller (also needed there for the BVH
        // force decision) -- this just computes the estimated on-screen size.
        float CullNodeAndGetNDCSize(const HLSL::Camera& camera, float invTanHalfFovY, const Node& node, float4 boundingSphere)
        {
            float dist = std::max((float)length(boundingSphere.xyz - camera.worldPos.xyz), 0.001f);
            return (node.size / dist) * 0.5f * invTanHalfFovY;
        }

        void Subdivide(const Node& node, std::vector<Node>& out)
        {
            float quarter = node.size * 0.25f;
            float half = node.size * 0.5f;
            static const float2 dirs[4] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };
            for (uint i = 0; i < 4; i++)
            {
                uint2 childCoord = node.coord * 2u + uint2(dirs[i].x > 0.0f ? 1u : 0u, dirs[i].y > 0.0f ? 1u : 0u);
                out.push_back({ node.center + dirs[i] * quarter, half, node.depth + 1, childCoord });
            }
        }

        // Frees an entity's override mesh slot (if any) before releasing it -- the ONE place that
        // must run at every node-instance destruction site (leftover sweep, mesh/material-swap
        // retire, removed-terrain teardown, ClearAll), so it's centralized here instead of
        // duplicated at each call site.
        void ReleaseNodeEntity(World::Entity ent)
        {
            if (!ent.IsValid())
                return;
            if (ent.Has<Components::MeshOverride>())
            {
                auto& ov = ent.Get<Components::MeshOverride>();
                if (ov.overrideMeshIndex != ~0u && MeshStorage::instance != nullptr)
                    MeshStorage::instance->FreeMeshOverride(ov.overrideMeshIndex, ov.sourceMeshIndex);
            }
            ent.ReleaseImmediately();
            churn.released++;
        }

        // Attempts to (re)populate a node's MeshOverride from the terrain's current gridMesh.
        // Called at node creation AND, if that attempt found the source mesh not resolvable yet,
        // retried every frame the node is revisited (EmitNodeInstance) until it succeeds -- an
        // async mesh load taking a few frames is the COMMON case right after a scene loads a
        // terrain from disk (AssetLibrary::Get with the default immediate=false just registers a
        // loading request and returns nullptr; only the synchronous "Build Grid Mesh" editor
        // button path resolves on the first try). Leaves the ~0u sentinel in place (no-op) when
        // it still isn't ready -- self-healing, no special one-time resync pass needed, and cheap
        // to retry (a failed attempt costs one Get<Mesh> call).
        // scaleXZ: the node's world-footprint-to-gridSize ratio. Node instances carry an identity
        // Transform.scale (baked meshes need no render-time scale), so this is the ONLY place that
        // ratio gets applied -- baked into the override's own AABB (X/Z scaled by it, matching what
        // the bake shader also bakes into the vertex positions themselves -- see
        // terrainMeshBake.hlsl) so the override's encode domain matches its actual baked content.
        void TryAttachOverride(World::Entity terrainEntity, Components::Terrain& terrain, Components::MeshOverride& ov, Components::TerrainBaking& baking, float scaleXZ)
        {
            Components::Mesh& gridMeshCmp = terrain.gridMesh.Get();
            Mesh* sourceMesh = AssetLibrary::instance->Get<Mesh>(gridMeshCmp.id);
            if (sourceMesh == nullptr)
                return;

            // X/Z are the source's own footprint scaled up to this node's real world size; only Y
            // is degenerate on the source (flat, no room to encode displacement into) and needs a
            // real range -- CreateMeshOverride itself has no terrain-specific knowledge of either.
            float aabbMinY, aabbExtentY;
            ComputeOverrideHeightRange(terrain, aabbMinY, aabbExtentY);
            float3 overrideAabbMin((float)sourceMesh->aabbMin.x * scaleXZ, aabbMinY, (float)sourceMesh->aabbMin.z * scaleXZ);
            float3 overrideAabbExtent((float)sourceMesh->aabbExtent.x * scaleXZ, aabbExtentY, (float)sourceMesh->aabbExtent.z * scaleXZ);
            ov.overrideMeshIndex = MeshStorage::instance->CreateMeshOverride(*sourceMesh, overrideAabbMin, overrideAabbExtent, AssetLibrary::instance->commandBuffer.Get());
            ov.sourceMeshIndex = sourceMesh->storageIndex;
            baking.terrain.Set(terrainEntity);
            ov.dirty = 1; // always bake at least once before this node is ever rendered
        }

        EntityBase MakeNodeInstance(World::Entity terrainEntity, Components::Terrain& terrain, float3 position, float scaleXZ, float4 boundingSphere)
        {
            ZoneScoped;
            World::Entity ent;
            ent.Make(Components::Transform::mask | Components::WorldMatrix::mask | Components::Instance::mask | Components::MeshOverride::mask | Components::TerrainBaking::mask);
            auto& tr = ent.Get<Components::Transform>();
            tr.position = position;
            tr.rotation = quaternion::identity();
            // Always identity: the node's world footprint (scaleXZ) is baked directly into the
            // override mesh's vertex positions and AABB instead (see TryAttachOverride /
            // terrainMeshBake.hlsl), not applied via this transform.
            tr.scale = float3(1.0f, 1.0f, 1.0f);
            auto& inst = ent.Get<Components::Instance>();
            inst.mesh = terrain.gridMesh;
            inst.material = terrain.material;
            inst.meshRT = Components::Handle<Components::Mesh>{ entityInvalid };
            inst.boundingSphereOverride = boundingSphere; // world-space, real per-node height range
            ent.Get<Components::State>().flags |= Components::State::Flags::transient;

            // Terrain RT/raster unification: give this node its own override mesh (own vertex
            // range + AABB + BLAS, sharing the source grid's meshlet/index/LOD data) so it gets
            // displaced ONCE on the GPU (TerrainErosion::Render's bake dispatch) instead of the
            // gbuffer path re-sampling the heightmap every frame, and enters the TLAS as real
            // geometry instead of a flat plane. Starts at the ~0u sentinel (no MeshOverride content
            // -> UpdateInstances/terrainmesh.hlsl both no-op, renders as an undisplaced flat patch)
            // until TryAttachOverride succeeds -- may take a few frames right after scene load (see
            // its comment).
            auto& ov = ent.Get<Components::MeshOverride>();
            ov.overrideMeshIndex = ~0u;
            ov.sourceMeshIndex = ~0u;
            ov.dirty = 0;
            auto& baking = ent.Get<Components::TerrainBaking>();
            baking.terrain.Set(entityInvalid);
            TryAttachOverride(terrainEntity, terrain, ov, baking, scaleXZ);

            return ent;
        }

        void EmitNodeInstance(World::Entity terrainEntity, Components::Terrain& terrain, float terrainY, const Node& node, float4 boundingSphere, std::unordered_map<uint64_t, PooledNodeInstance>& pool)
        {
            if (!terrain.gridMesh.IsValid() || !terrain.material.IsValid())
                return;

            float gridSize = terrain.gridSize > 0.001f ? terrain.gridSize : 0.001f;
            float3 position = float3(node.center.x, terrainY, node.center.y);
            float scaleXZ = node.size / gridSize;
            uint64_t key = NodeKey(node);

            auto it = pool.find(key);
            if (it != pool.end())
            {
                World::Entity ent = it->second.entity;
                if (ent.IsValid())
                {
                    auto& inst = ent.Get<Components::Instance>();
                    if (inst.mesh == terrain.gridMesh && inst.material == terrain.material)
                    {
                        auto& tr = ent.Get<Components::Transform>();
                        // tr.scale is always identity now (see MakeNodeInstance) -- a footprint
                        // (size) change is caught via boundingSphereOverride instead, which node.size
                        // directly determines (ComputeNodeBoundingSphere), so it's already a
                        // reliable proxy without needing scaleXZ compared here too.
                        bool changed =
                            (float)tr.position.x != (float)position.x ||
                            (float)tr.position.y != (float)position.y ||
                            (float)tr.position.z != (float)position.z ||
                            (float)inst.boundingSphereOverride.x != (float)boundingSphere.x ||
                            (float)inst.boundingSphereOverride.y != (float)boundingSphere.y ||
                            (float)inst.boundingSphereOverride.z != (float)boundingSphere.z ||
                            (float)inst.boundingSphereOverride.w != (float)boundingSphere.w;
                        if (changed)
                        {
                            tr.position = position;
                            tr.version++;
                            inst.boundingSphereOverride = boundingSphere;
                            ent.Get<Components::State>().flags |= Components::State::Flags::dirty;
                            churn.reusedRewritten++;

                            // scaleXZ is baked into the override's own AABB/vertex positions (see
                            // TryAttachOverride), not applied via tr.scale -- a footprint change can
                            // mean that baked scale is stale too, not just the vertex content a mere
                            // re-bake (dirty=1) would refresh, so free the current slot and fall
                            // through to the retry path below, which reattaches fresh at the new
                            // scaleXZ (recycled from the same source's free list, not a real
                            // reallocation -- see MeshStorage::FreeMeshOverride).
                            if (ent.Has<Components::MeshOverride>())
                            {
                                auto& ov = ent.Get<Components::MeshOverride>();
                                if (ov.overrideMeshIndex != ~0u)
                                {
                                    MeshStorage::instance->FreeMeshOverride(ov.overrideMeshIndex, ov.sourceMeshIndex);
                                    ov.overrideMeshIndex = ~0u;
                                }
                            }
                        }
                        else
                            churn.reusedUnchanged++;

                        // Retry attaching an override every frame it's still missing -- either the
                        // source mesh wasn't resolvable at creation time (see TryAttachOverride), or
                        // a footprint change just freed it above.
                        if (ent.Has<Components::MeshOverride>())
                        {
                            auto& ov = ent.Get<Components::MeshOverride>();
                            if (ov.overrideMeshIndex == ~0u)
                            {
                                TryAttachOverride(terrainEntity, terrain, ov, ent.Get<Components::TerrainBaking>(), scaleXZ);
                                // A (re)attach that just succeeded needs UpdateInstances (Renderer.h)
                                // to re-upload this instance -- it currently points at the
                                // fallback source mesh, and won't be revisited otherwise (its
                                // "loaded && !dirty" skip only reacts to Components::State::dirty,
                                // separate from MeshOverride::dirty above).
                                if (ov.overrideMeshIndex != ~0u)
                                    ent.Get<Components::State>().flags |= Components::State::Flags::dirty;
                            }
                        }

                        it->second.lastTouchedFrame = frameCounter;
                        return;
                    }
                    ReleaseNodeEntity(ent); // mesh/material swapped: retire, remake fresh below
                }
                it->second.entity = MakeNodeInstance(terrainEntity, terrain, position, scaleXZ, boundingSphere);
                it->second.lastTouchedFrame = frameCounter;
                churn.created++;
                return;
            }
            pool.emplace(key, PooledNodeInstance{ MakeNodeInstance(terrainEntity, terrain, position, scaleXZ, boundingSphere), frameCounter });
            churn.created++;
        }

        void CullQuadtree(World::Entity terrainEntity, Components::Terrain& terrain, float terrainY, const TerrainHeightPyramid& pyramid,
            const HLSL::Camera& camera, float invTanHalfFovY,
            std::vector<Node>& nodesToSubdivide, std::vector<Node>& newNodesToSubdivide,
            std::unordered_map<uint64_t, PooledNodeInstance>& pool)
        {
            ZoneScoped;
            for (auto& node : nodesToSubdivide)
            {
                float4 boundingSphere = ComputeNodeBoundingSphere(terrain, terrainY, pyramid, node);
                bool frustumCulled = FrustumCulled(camera, boundingSphere);

                // Omnidirectional (NOT frustum-gated) forced depth around the camera: keeps
                // terrain resident in the TLAS for RT shadow rays, whose casters aren't
                // necessarily camera-frustum-visible -- culling.hlsl's RT instance culling is
                // already distance-only/omnidirectional, it just never sees instances this CPU
                // walk dropped in the first place. Full BVHMaxDepth is forced within
                // BVHMinRadius, linearly relaxed to 0 by BVHMaxRadius; BVHMaxRadius <=
                // BVHMinRadius (the default) disables this entirely.
                bool bvhActive = terrain.BVHMaxRadius > terrain.BVHMinRadius;
                float distToCam = (float)length(boundingSphere.xyz - camera.worldPos.xyz);
                bool bvhRelevant = bvhActive && distToCam < terrain.BVHMaxRadius;
                uint forcedDepth = 0;
                if (bvhActive)
                {
                    float t = std::clamp((distToCam - terrain.BVHMinRadius) / (terrain.BVHMaxRadius - terrain.BVHMinRadius), 0.0f, 1.0f);
                    forcedDepth = (uint)std::round((1.0f - t) * terrain.BVHMaxDepth);
                }

                if (frustumCulled && !bvhRelevant)
                    continue; // neither raster-visible nor BVH/shadow-relevant

                float size = frustumCulled ? -1.0f : CullNodeAndGetNDCSize(camera, invTanHalfFovY, node, boundingSphere);
                bool subdivideForLOD = !frustumCulled && size > terrain.targetScreenSize && node.depth < terrain.quadtreeDepth;
                bool subdivideForBVH = bvhRelevant && node.depth < forcedDepth;

                if (subdivideForLOD || subdivideForBVH)
                    Subdivide(node, newNodesToSubdivide);
                else
                    EmitNodeInstance(terrainEntity, terrain, terrainY, node, boundingSphere, pool);
            }
        }

        void Update(World* world) override
        {
            ZoneScoped;

            churn = {};
            frameCounter++;

            uint terrainQueryIndex = world->Query(Components::Terrain::mask, 0, true);
            auto& terrains = world->frameQueries[terrainQueryIndex];
            if (terrains.empty()) { ClearAll(); PublishChurn(); return; }

            if (Components::getMainCamera == nullptr)
            {
                PublishChurn();
                return;
            }
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

                // Terrain's material is now shaded like a standard mesh (PixelgBufferTerrain,
                // terrainmesh.hlsl) -- displacement params go straight from this Components::Terrain
                // into HLSL::TerrainBakeParameters (Renderer.h), not through the material at all.

                // Procedural terrains have no imported texture asset at all -- terrain.heightmap
                // may be a never-explicitly-assigned Handle, and raw-malloc'd ECS component
                // storage has no zero-init guarantee, so IsValid() (a sentinel-id check, not an
                // "was this ever set" check) can't be trusted to read false here. Skip straight to
                // the conservative fallback pyramid (full [0,1] raw-height range) instead of
                // risking BuildTerrainHeightPyramid treating garbage bytes as a real entity.
                auto& heightData = heightPyramids[e.id];
                if (terrain.useProceduralHeightmap || !terrain.heightmap.IsValid())
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

                auto& pool = instancesPerTerrain[e.id];

                auto& nodesToSubdivide = nodeScratch[0];
                auto& newNodesToSubdivide = nodeScratch[1];
                nodesToSubdivide.clear();
                nodesToSubdivide.push_back({ float2(origin.x, origin.z), terrain.worldExtent, 0, uint2(0, 0) });
                while (!nodesToSubdivide.empty())
                {
                    newNodesToSubdivide.clear();
                    CullQuadtree(e, terrain, origin.y, heightData.pyramid, camera, invTanHalfFovY, nodesToSubdivide, newNodesToSubdivide, pool);
                    std::swap(nodesToSubdivide, newNodesToSubdivide);
                }

                {
                    ZoneScopedN("ReleaseLeftoverInstances");
                    for (auto it = pool.begin(); it != pool.end();)
                    {
                        if (it->second.lastTouchedFrame != frameCounter)
                        {
                            ReleaseNodeEntity(it->second.entity);
                            it = pool.erase(it);
                        }
                        else
                            ++it;
                    }
                }
            }

            auto stillPresent = [&](uint id) { return std::find(presentTerrains.begin(), presentTerrains.end(), id) != presentTerrains.end(); };
            for (auto it = instancesPerTerrain.begin(); it != instancesPerTerrain.end();)
            {
                if (!stillPresent(it->first))
                {
                    for (auto& nodeKv : it->second) ReleaseNodeEntity(nodeKv.second.entity);
                    it = instancesPerTerrain.erase(it);
                }
                else ++it;
            }
            for (auto it = heightPyramids.begin(); it != heightPyramids.end();)
            {
                if (!stillPresent(it->first)) it = heightPyramids.erase(it);
                else ++it;
            }

            PublishChurn();

            //world->structureChanged = true;
        }
    };
}