#pragma once

class AssetLibrary
{
    std::shared_mutex lock;
public:
    enum class AssetType
    {
        mesh,
        shader,
        texture
    };
    struct Asset
    {
        String path;
        AssetLibrary::AssetType type;
        uint indexInVector = ~0;
    };
    static AssetLibrary* instance;
    std::unordered_map<assetID, Asset> map;
    String assetsPath = "..\\Assets\\";
    String file = "..\\assetLibrary.txt";
    std::unordered_map<assetID, uint> loadingRequest;
    int meshLoadingLimit = 5;
    int shaderLoadingLimit = 5;
    int textureLoadingLimit = 5;
    int meshLoaded = 0;
    int shaderLoaded = 0;
    int textureLoaded = 0;

    std::vector<Mesh> meshes;
    std::vector<Shader> shaders;
    std::vector<Resource> textures;

    PerFrame<CommandBuffer> commandBuffer;
    const char* name = "AssetLibraryUpload";

    void On()
    {
        instance = this;
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i).On(GPU::instance->computeQueue, name); //not copyQueue because creation of blas needs it :(
        }
        namespace fs = std::filesystem;
        fs::create_directories("..\\Assets");
        Load();
    }

    void Off()
    {
        Save();
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i).Off();
        }
        for (uint i = 0; i < meshes.size(); i++)
        {
            meshes[i].BLAS.Release();
        }
        for (uint i = 0; i < shaders.size(); i++)
        {
            shaders[i].shaderBindingTable.Release();
        }
        instance = nullptr;
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
        PIXBeginEvent(commandBuffer->cmd, PIX_COLOR_INDEX((BYTE)name), name);
#endif
        Profiler::instance->StartProfile(commandBuffer.Get(), name);
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
            IOs::Log("{} OPEN !!", name);

        if (endOfLastFrame != nullptr)
        {
            uint lastFrameIndex = GPU::instance->frameIndex ? 0 : 1;
            commandBuffer->queue->Wait(endOfLastFrame->Get(lastFrameIndex).passEnd.fence, endOfLastFrame->Get(lastFrameIndex).passEnd.fenceValue);
        }
        commandBuffer->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&commandBuffer->cmd);
        commandBuffer->queue->Signal(commandBuffer->passEnd.fence, ++commandBuffer->passEnd.fenceValue);
    }

    void CheckReload()
    {
        for (auto& item : map)
        {
            if (item.second.type == AssetLibrary::AssetType::shader)
            {
                if (map[item.first].indexInVector != ~0)
                {
                    if (Get<Shader>(item.first)->NeedReload())
                    {
                        LoadAsset(item.first, false);
                    }
                }
            }
        }
    }

    AssetLibrary::AssetType GetType(assetID id)
    {
        String path = GetPath(id);
        int extentionStart = path.find_last_of('.') + 1;
        String extenstion = path.substr(extentionStart);
        AssetLibrary::AssetType type;
        if (extenstion == "mesh") type = AssetLibrary::AssetType::mesh;
        else if (extenstion == "hlsl") type = AssetLibrary::AssetType::shader;
        else if (extenstion == "dds") type = AssetLibrary::AssetType::texture;

        return type;
    }

    assetID Add(String path)
    {
        ZoneScoped;
        assetID id;
        id.hash = std::hash<std::string>{}(path);
        lock.lock();
        map[id].path = path;
        map[id].type = GetType(id);
        lock.unlock();
        return id;
    }

    String GetPath(assetID id)
    {
        seedAssert(map.contains(id));
        return map[id].path;
    }

    uint GetIndex(assetID id)
    {
        seedAssert(map.contains(id));
        auto& asset = map[id];
        if (asset.indexInVector == ~0)
        {
            lock.lock();
            loadingRequest[id]++;
            lock.unlock();
        }
        return asset.indexInVector;
    }

    template<typename T>
    T* Get(assetID id, bool immediate = false)
    {
        seedAssert(map.contains(id));
        auto& asset = map[id];
        if (asset.indexInVector == ~0)
        {
            if (!immediate)
            {
                lock.lock();
                loadingRequest[id]++;
                lock.unlock();
                return nullptr;
            }
            else
            {
                LoadAsset(id, true);
            }
        }

        if (asset.indexInVector != ~0)
        {
            if (asset.type == AssetLibrary::AssetType::mesh)
                return (T*)&meshes[asset.indexInVector];
            else if (asset.type == AssetLibrary::AssetType::shader)
                return (T*)&shaders[asset.indexInVector];
            else if (asset.type == AssetLibrary::AssetType::texture)
                return (T*)&textures[asset.indexInVector];
        }

        return nullptr;
    }

    void LoadAssets();
    void LoadAsset(assetID id, bool ignoreBudget);

    void Save()
    {
        ZoneScoped;
        String line;
        std::ofstream myfile(file);
        if (myfile.is_open())
        {
            for (auto& item : map)
            {
                myfile << item.first.hash << " " << item.second.path << std::endl;
            }
        }
    }

    void Load()
    {
        ZoneScoped;
        String line;
        std::ifstream myfile(file);
        if (myfile.is_open())
        {
            assetID id;
            String path;
            while (myfile >> id.hash >> path)
            {
                map[id].path = path;
                map[id].type = GetType(id);
            }
        }
    }
};
AssetLibrary* AssetLibrary::instance = nullptr;

#include <wincodec.h>
#include "../../Third/DirectXTex-main/WICTextureLoader/WICTextureLoader12.h"
#include "../../Third/DirectXTex-main/DDSTextureLoader/DDSTextureLoader12.h"
class TextureLoader
{
public:
    static TextureLoader* instance;
    IWICImagingFactory* wicFactory = NULL;
    void On()
    {
		ZoneScoped;
        instance = this;
        HRESULT hr;
        CoInitialize(NULL);// Initialize the COM library
        hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));// create the WIC factory
    }

    void Off()
    {
		ZoneScoped;
        wicFactory->Release();
        instance = nullptr;
    }

    bool Load(Resource& resource, String path)
    {
        return false;
    }
};
TextureLoader* TextureLoader::instance = nullptr;

#include "../../Third/meshoptimizer-master/src/meshoptimizer.h"
class MeshLoader
{
public:
    static MeshLoader* instance;

    struct MeshOriginal
    {
        std::vector<uint> indices;
        std::vector<Vertex> vertices;
        float4 boundingSphere;
    };


	void On()
	{
		ZoneScoped;
        instance = this;
	}

	void Off()
	{
		ZoneScoped;
        instance = nullptr;
	}

    MeshData Read(String path)
    {
		ZoneScoped;
        MeshData mesh;

        std::ifstream fin(path, std::ios::binary);
        if (fin.is_open())
        {
#define READ_VECTOR(vector)  { size_t size; fin.read((char*)&size, sizeof(size)); vector.resize(size); fin.read((char*)&vector[0], size * sizeof(vector[0])); }
            READ_VECTOR(mesh.meshlets);
            READ_VECTOR(mesh.meshlet_triangles);
            READ_VECTOR(mesh.meshlet_vertices);
            READ_VECTOR(mesh.vertices);
            READ_VECTOR(mesh.indices);
            fin.read((char*)&mesh.boundingSphere, sizeof(mesh.boundingSphere));
            fin.close();
        }

        return mesh;
    }


    assetID Write(MeshData& mesh, String name)
    {
        ZoneScoped;
        String path;
        assetID id;
        id.hash = std::hash<std::string>{}(name);
        path = std::format("{}{}.mesh", AssetLibrary::instance->assetsPath.c_str(), id.hash);

        std::ofstream fout(path, std::ios::binary);
        if (fout.is_open())
        {
#define WRITE_VECTOR(vector)  { size_t size = vector.size(); fout.write((char*)&size, sizeof(size)); fout.write((char*)&vector[0], size * sizeof(vector[0])); }
            WRITE_VECTOR(mesh.meshlets);
            WRITE_VECTOR(mesh.meshlet_triangles);
            WRITE_VECTOR(mesh.meshlet_vertices);
            WRITE_VECTOR(mesh.vertices);
            WRITE_VECTOR(mesh.indices);
            fout.write((char*)&mesh.boundingSphere, sizeof(mesh.boundingSphere));
            fout.close();
        }

        return AssetLibrary::instance->Add(path);
    }

    // also DirectXMesh can do meshlets https://github.com/microsoft/DirectXMesh
    MeshData Process(MeshOriginal& originalMesh)
    {
		ZoneScoped;
        const float cone_weight = 0.5f;

        //meshopt_generateVertexRemap

        size_t max_meshlets = meshopt_buildMeshletsBound(originalMesh.indices.size(), HLSL::max_vertices, HLSL::max_triangles);
        std::vector<meshopt_Meshlet> meshopt_meshlets(max_meshlets);
        std::vector<unsigned int> meshlet_vertices(max_meshlets * HLSL::max_vertices);
        std::vector<unsigned char> meshlet_triangles(max_meshlets * HLSL::max_triangles * 3);

        size_t meshlet_count = meshopt_buildMeshlets(meshopt_meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), originalMesh.indices.data(), originalMesh.indices.size(), &originalMesh.vertices[0].px, originalMesh.vertices.size(), sizeof(Vertex), HLSL::max_vertices, HLSL::max_triangles, cone_weight);


        std::vector<Meshlet> meshlets(meshlet_count);
        meshlets.resize(meshlet_count);
        for (uint i = 0; i < meshlet_count; i++)
        {
            auto& m = meshopt_meshlets[i];
            meshopt_optimizeMeshlet(&meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset], m.triangle_count, m.vertex_count);
            meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset], m.triangle_count, &originalMesh.vertices[0].px, originalMesh.vertices.size(), sizeof(Vertex));

            meshlets[i].triangleCount = m.triangle_count;
            meshlets[i].triangleOffset = m.triangle_offset;
            meshlets[i].vertexCount = m.vertex_count;
            meshlets[i].vertexOffset = m.vertex_offset;
            meshlets[i].boundingSphere = float4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
        }

        const meshopt_Meshlet& last = meshopt_meshlets[meshlet_count - 1];
        meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
        meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
        //meshopt_meshlets.resize(meshlet_count);

        MeshData optimizedMesh;
        optimizedMesh.meshlets = meshlets;
        optimizedMesh.meshlet_vertices = meshlet_vertices;
        optimizedMesh.meshlet_triangles = meshlet_triangles;
        optimizedMesh.vertices = originalMesh.vertices;
        optimizedMesh.indices = originalMesh.indices;
        optimizedMesh.boundingSphere = originalMesh.boundingSphere;
        
        for (uint i = 0; i < optimizedMesh.meshlets.size(); i++)
        {
            seedAssert(optimizedMesh.meshlets[i].vertexCount > 0);
            seedAssert(optimizedMesh.meshlets[i].triangleCount > 0);
            seedAssert(optimizedMesh.meshlets[i].vertexCount <= HLSL::max_vertices);
            seedAssert(optimizedMesh.meshlets[i].triangleCount <= HLSL::max_triangles);
            seedAssert(optimizedMesh.meshlets[i].vertexOffset < optimizedMesh.meshlet_vertices.size());
            seedAssert(optimizedMesh.meshlets[i].triangleOffset < optimizedMesh.meshlet_triangles.size());
            //IOs::Log("V {} | T {}", optimizedMesh.meshlets[i].vertexCount, optimizedMesh.meshlets[i].triangleCount);
        }

        return optimizedMesh;

        //use if (dot(normalize(cone_apex - camera_position), cone_axis) >= cone_cutoff) reject(); in mesh shader for cone culling
    }
};
MeshLoader* MeshLoader::instance = nullptr;

#include "../../Third/assimp-master/include/assimp/Importer.hpp"
#include "../../Third/assimp-master/include/assimp/Exporter.hpp"
#include "../../Third/assimp-master/include/assimp/scene.h"
#include "../../Third/assimp-master/include/assimp/postprocess.h"
class SceneLoader
{
    std::vector<World::Entity> meshIndexToEntity;
    std::vector<World::Entity> matIndexToEntity;
    std::vector<World::Entity> animationIndexToEntity;
    std::vector<World::Entity> skeletonIndexToEntity;

public:
    static SceneLoader* instance;
    void On()
    {
        instance = this;
    }

    void Off()
    {
        instance = nullptr;
    }

    void Load(String path)
    {
        ZoneScoped;

        IOs::Log("Loading : {}", path.c_str());

        Assimp::Importer importer;
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, true);
        importer.SetPropertyBool(AI_CONFIG_FBX_CONVERT_TO_M, true);
        const aiScene* _scene = importer.ReadFile(path,
            0x0
            // for DX
            | aiProcess_MakeLeftHanded
            | aiProcess_FlipWindingOrder
            | aiProcess_FlipUVs
            
            //| aiProcess_CalcTangentSpace
            //| aiProcess_FixInfacingNormals
            //| aiProcess_GenSmoothNormals
            //| aiProcess_GenNormals
            | aiProcess_Triangulate
            //| aiProcess_JoinIdenticalVertices
            //| aiProcess_SortByPType
            //| aiProcess_FindInvalidData
            | aiProcess_FindInstances
            | aiProcess_GlobalScale
            //| aiProcess_GenBoundingBoxes
            //| aiProcess_RemoveRedundantMaterials
            //| aiProcess_OptimizeGraph // <- ca fuck les mesh en bakant la matrice dans les vertex pos non ?
            //| aiProcess_OptimizeMeshes

            //| aiProcess_PopulateArmatureData
            //| aiProcess_LimitBoneWeights
            //| aiProcess_Debone
        );

        if (!_scene)
        {
            return;
        }

        ai_real unitSize(0.0);
        _scene->mMetaData->Get("UnitScaleFactor", unitSize);
        if (unitSize == 0)
            _scene->mMetaData->Get("OriginalUnitScaleFactor", unitSize);

        if (unitSize != 0)
        {
            unitSize = 1 / unitSize;
            _scene->mRootNode->mTransformation *= aiMatrix4x4(unitSize, 0, 0, 0,
                0, unitSize, 0, 0,
                0, 0, unitSize, 0,
                0, 0, 0, 1);
        }
        IOs::Log("  File unit scale : {}", unitSize);

        CreateAnimations(_scene);
        CreateMeshes(_scene);
        CreateMaterials(_scene);

        //for (uint i = 0; i < 100; i++)
        {
            //_scene->mRootNode->mTransformation.a4 = Rand01() * 5.0f;
            //_scene->mRootNode->mTransformation.b4 = Rand01() * 5.0f;
            //_scene->mRootNode->mTransformation.c4 = Rand01() * 5.0f;

            CreateEntities(_scene, _scene->mRootNode);
        }

        meshIndexToEntity.clear();
        matIndexToEntity.clear();
        animationIndexToEntity.clear();
        skeletonIndexToEntity.clear();
    }

    World::Entity CreateEntities(const aiScene* _scene, aiNode* node, World::Entity parentEntity = entityInvalid)
    {
        ZoneScoped;

        //need to transpose matrices ?
        aiVector3D _pos;
        aiQuaternion _rot;
        aiVector3D _scale;
        node->mTransformation.Decompose(_scale, _rot, _pos);

        float3 pos;
        quaternion rot;
        float3 scale;

        pos.x = _pos.x;
        pos.y = _pos.y;
        pos.z = _pos.z;

        rot.x = _rot.x;
        rot.y = _rot.y;
        rot.z = _rot.z;
        rot.w = _rot.w;

        scale.x = _scale.x;
        scale.y = _scale.y;
        scale.z = _scale.z;

        World::Entity ent;
        Components::Mask mask = (Components::Transform::mask | Components::WorldMatrix::mask | Components::Name::mask);
        if (parentEntity != entityInvalid)
            mask |= Components::Parent::mask;
        if (node->mNumMeshes == 0)
        {
            ent.Make(mask);

            auto& name = ent.Get<Components::Name>();
            strcpy_s(name.name, 256, node->mName.C_Str());

            auto& transform = ent.Get<Components::Transform>();
            transform.position = pos;
            transform.rotation = rot;
            transform.scale = scale;

            if (parentEntity != entityInvalid)
            {
                auto& parent = ent.Get<Components::Parent>();
                parent.entity = { parentEntity.id };
            }
        }
        else
        {
            mask |= Components::Instance::mask;

            // if node has meshes, create a new scene object for it
            for (unsigned int i = 0; i < node->mNumMeshes; i++)
            {
                ent.Make(mask);

                auto& name = ent.Get<Components::Name>();
                strcpy_s(name.name, 256, node->mName.C_Str());

                auto& transform = ent.Get<Components::Transform>();
                transform.position = pos;
                transform.rotation = rot;
                transform.scale = scale;

                if (parentEntity != entityInvalid)
                {
                    auto& parent = ent.Get<Components::Parent>();
                    parent.entity = { parentEntity.id };
                }

                auto& instance = ent.Get<Components::Instance>();
                instance.mesh = Components::Handle<Components::Mesh>{ meshIndexToEntity[node->mMeshes[i]].id };
                instance.material = Components::Handle<Components::Material>{ matIndexToEntity[_scene->mMeshes[node->mMeshes[i]]->mMaterialIndex].id };
            }
        }


        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            CreateEntities(_scene, node->mChildren[i], ent);
        }

        return ent;
    }

    void CreateMeshes(const aiScene* _scene)
    {
        ZoneScoped;
        IOs::Log("  meshes : {}", _scene->mNumMeshes);
        for (unsigned int i = 0; i < _scene->mNumMeshes; i++)
        {
            auto m = _scene->mMeshes[i];
            //IOs::Log("    {} {}", m->mName.C_Str(), i);
            {
                MeshLoader::MeshOriginal originalMesh;
                float3 minBB = float3(FLT_MAX);
                float3 maxBB = float3(FLT_MIN);

                originalMesh.vertices.resize(m->mNumVertices);
                for (unsigned int j = 0; j < m->mNumVertices; j++)
                {
                    Vertex& v = originalMesh.vertices[j];

                    v.px = m->mVertices[j].x;
                    v.py = m->mVertices[j].y;
                    v.pz = m->mVertices[j].z;

                    minBB.x = min(minBB.x, v.px);
                    minBB.y = min(minBB.x, v.py);
                    minBB.z = min(minBB.x, v.pz);

                    maxBB.x = max(maxBB.x, v.px);
                    maxBB.y = max(maxBB.x, v.py);
                    maxBB.z = max(maxBB.x, v.pz);

                    if (m->HasNormals())
                    {
                        v.nx = m->mNormals[j].x;
                        v.ny = m->mNormals[j].y;
                        v.nz = m->mNormals[j].z;
                    }
                    if (m->HasTextureCoords(0))
                    {
                        v.u = m->mTextureCoords[0][j].x;
                        v.v = m->mTextureCoords[0][j].y;
                    }
                    /*
                    if (m->HasTangentsAndBitangents())
                    {
                        v->tangent = *(float4*)(&m->mTangents[j]);
                        v->binormal = *(float4*)(&m->mBitangents[j]);
                    }
                    */
                }

                // pour le moment ca marche que pour du triangul� (le 3)
                originalMesh.indices.resize(m->mNumFaces * 3);
                unsigned int index = 0;
                for (unsigned int j = 0; j < m->mNumFaces; j++)
                {
                    for (unsigned int k = 0; k < m->mFaces[j].mNumIndices; k++)
                    {
                        originalMesh.indices[index] = m->mFaces[j].mIndices[k];
                        index++;
                    }
                }

                float3 center = (minBB + maxBB) * 0.5f;
                float radius = length(minBB - maxBB) * 0.5f;
                originalMesh.boundingSphere = float4(center, radius);

                MeshData mesh = MeshLoader::instance->Process(originalMesh);
                assetID id = MeshLoader::instance->Write(mesh, std::format("{}{}", m->mName.C_Str(), i));

                World::Entity ent;
                ent.Make(Components::Mesh::mask);
                ent.Get<Components::Mesh>().id = id;

                meshIndexToEntity.push_back(ent);
            }
        }
    }

    void CreateMaterials(const aiScene* _scene)
    {
        ZoneScoped;

        World::Entity shader;
        shader.Make(Components::Shader::mask);
        shader.Get<Components::Shader>().id = AssetLibrary::instance->Add("src\\Shaders\\mesh.hlsl");

        IOs::Log("  materials : {}", _scene->mNumMaterials);
        for (unsigned int i = 0; i < _scene->mNumMaterials; i++)
        {
            std::cout << ".";
            auto m = _scene->mMaterials[i];
            World::Entity ent;
            ent.Make(Components::Material::mask | Components::Name::mask);

            auto& name = ent.Get<Components::Name>();
            strcpy_s(name.name, 256, m->GetName().C_Str());

            auto& newMat = ent.Get<Components::Material>();

            aiString texName;

            for (unsigned int j = 0; j < 17; j++)
            {
                for (unsigned int k = 0; k < m->GetTextureCount((aiTextureType)j); k++)
                {
                    texName = "";
                    m->GetTexture((aiTextureType)j, k, &texName);
                    std::cout << texName.C_Str() << "\n";
                }
            }

            newMat.shader = { shader.id };

            for (uint j = 0; j < newMat.maxTextures; j++)
            {
                newMat.textures[j] = { entityInvalid };
            }

            /*
            texName = "";
            m->GetTexture(aiTextureType_DIFFUSE, 0, &texName);
            if (texName.length == 0) m->GetTexture(aiTextureType_BASE_COLOR, 0, &texName);
            //if (texName.length == 0) texName = file.substr(0, file.length() - 8) + "4k_" + "Albedo" + ".jpg";
            auto res = GetOrCreateResource(world, texName.C_Str());
            //newMat.textures[0] = res;
            AssignTexture(gpuShader, newMat.textures, "albedo", res);

            texName = "";
            m->GetTexture(aiTextureType_NORMALS, 0, &texName);
            if (texName.length == 0) m->GetTexture(aiTextureType_HEIGHT, 0, &texName);
            if (texName.length == 0) m->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &texName);
            //if (texName.length == 0) texName = file.substr(0, file.length() - 8) + "4k_" + "Normal_LOD0" + ".jpg";
            res = GetOrCreateResource(world, texName.C_Str());
            //newMat.textures[1] = res;
            AssignTexture(gpuShader, newMat.textures, "normal", res);

            texName = "";
            m->GetTexture(aiTextureType_METALNESS, 0, &texName);
            if (texName.length == 0) m->GetTexture(aiTextureType_SPECULAR, 0, &texName);
            //if (texName.length == 0) texName = file.substr(0, file.length() - 8) + "4k_" + "Albedo" + ".jpg";
            res = GetOrCreateResource(world, texName.C_Str());
            //newMat.textures[2] = res;
            AssignTexture(gpuShader, newMat.textures, "metalness", res);

            texName = "";
            m->GetTexture(aiTextureType_SHININESS, 0, &texName);
            if (texName.length == 0) m->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texName);
            //if (texName.length == 0) texName = file.substr(0, file.length() - 8) + "4k_" + "Roughness" + ".jpg";
            res = GetOrCreateResource(world, texName.C_Str());
            //newMat.textures[3] = res;
            AssignTexture(gpuShader, newMat.textures, "smoothness", res);

            texName = "";
            m->GetTexture(aiTextureType_AMBIENT, 0, &texName);
            if (texName.length == 0) m->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texName);
            //if (texName.length == 0) texName = file.substr(0, file.length() - 8) + "4k_" + "Cavity" + ".jpg";
            res = GetOrCreateResource(world, texName.C_Str());
            //newMat.textures[4] = res;
            AssignTexture(gpuShader, newMat.textures, "occlusion", res);

            texName = "";
            m->GetTexture(aiTextureType_EMISSIVE, 0, &texName);
            if (texName.length == 0) m->GetTexture(aiTextureType_EMISSION_COLOR, 0, &texName);
            //if (texName.length == 0) texName = file.substr(0, file.length() - 8) + "4k_" + "Cavity" + ".jpg";
            res = GetOrCreateResource(world, texName.C_Str());
            //newMat.textures[5] = res;
            AssignTexture(gpuShader, newMat.textures, "emission", res);

            AssignVector(gpuShader, newMat, "color", float4(1, 1, 1, 0));
            */

            matIndexToEntity.push_back(ent);
        }
    }

    void CreateAnimations(const aiScene* _scene)
    {
        ZoneScoped;
    }
};
SceneLoader* SceneLoader::instance = nullptr;

#pragma comment(lib, "dxcompiler.lib")
#include "../../Third/DirectXShaderCompiler-main/inc/dxcapi.h"
class ShaderLoader
{
public :
    static ShaderLoader* instance;
    IDxcUtils* DxcUtils{};
    IDxcCompiler3* DxcCompiler{};

    void On()
    {
        ZoneScoped;
        instance = this;
        HRESULT hr;
        hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&DxcUtils));
        if (FAILED(hr)) GPU::PrintDeviceRemovedReason(hr);
        hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&DxcCompiler));
        if (FAILED(hr)) GPU::PrintDeviceRemovedReason(hr);
    }

	void Off()
	{
		ZoneScoped;
		DxcUtils->Release();
		DxcCompiler->Release();
        instance = nullptr;
	}

    D3D12_SHADER_BYTECODE Compile(String file, String entry, String type, Shader* shader = nullptr)
    {
		ZoneScoped;
        bool compiled = false;

        auto wfile = file.ToWString();
        auto includePath = std::wstring(L"src\\Shaders\\");
        auto entryName = std::wstring(entry.ToWString());
        auto typeName = std::wstring(type.ToWString());

        HRESULT hr;
        ID3DBlob* errorBuff = NULL; // a buffer holding the error data if any
        ID3DBlob* signature = NULL;

        D3D12_SHADER_BYTECODE shaderBytecode{};
        IDxcBlobEncoding* source = nullptr;
        DxcUtils->LoadFile(wfile.c_str(), nullptr, &source);
        DxcBuffer Source;
        Source.Ptr = source->GetBufferPointer();
        Source.Size = source->GetBufferSize();
        Source.Encoding = DXC_CP_ACP;

        // Create default include handler.
        IDxcIncludeHandler* pIncludeHandler;
        DxcUtils->CreateDefaultIncludeHandler(&pIncludeHandler);
        IDxcBlob* pincludes = nullptr;
        pIncludeHandler->LoadSource(wfile.c_str(), &pincludes);

        std::vector<LPCWSTR> vArgs;
        vArgs.push_back(L"-I");
        vArgs.push_back(includePath.c_str());
        vArgs.push_back(L"-E");
        vArgs.push_back(entryName.c_str());
        vArgs.push_back(L"-T");
        vArgs.push_back(typeName.c_str());
        vArgs.push_back(L"-rootsig-define");
        vArgs.push_back(L"SeeDRootSignature");
        //vArgs.push_back(DXC_ARG_ALL_RESOURCES_BOUND);
        //vArgs.push_back(L"-no-warnings");
#ifdef _DEBUG
        vArgs.push_back(L"-Zi");
        vArgs.push_back(L"-Zss");
        vArgs.push_back(L"-Qembed_debug");
        vArgs.push_back(DXC_ARG_DEBUG);
        vArgs.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
        //vArgs.push_back(L"--hlsl-dxil-pix-shader-access-instrumentation");
#else
        vArgs.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);
        vArgs.push_back(L"-Zi");
        vArgs.push_back(L"-Qembed_debug");
        //vArgs.push_back(L"-Qstrip_debug");
        //vArgs.push_back(L"-Qstrip_reflect");
        //vArgs.push_back(L"-remove-unused-functions");
        //vArgs.push_back(L"-remove-unused-globals");
#endif

        // Compile it with specified arguments.
        IDxcResult* pResults;
        DxcCompiler->Compile( &Source, vArgs.data(), (UINT32)vArgs.size(), pIncludeHandler, IID_PPV_ARGS(&pResults));

        // Print errors if present.
        IDxcBlobUtf8* pErrors = nullptr;
        pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
        if (pErrors != nullptr && pErrors->GetStringLength() != 0)
        {
            std::string errorMsg = std::string((char*)pErrors->GetStringPointer());
            IOs::Log("---------------------- {} COMPILE FAILED -------------------", file.c_str());
            IOs::Log(errorMsg);
            return D3D12_SHADER_BYTECODE{};
        }
        // Quit if the compilation failed.
        HRESULT hrStatus;
        pResults->GetStatus(&hrStatus);
        if (!SUCCEEDED(hrStatus))
        {
            return D3D12_SHADER_BYTECODE{};
        }

        shaderBytecode = CreateShaderByteCode(shader, pResults);

        if (shader != nullptr)
        {
            CreateRootSignature(shader, pResults);
            CreateCommandSignature(shader);
            CreateRTShaderLibrary(shader, pResults);
        }

        ShaderReflection(pResults, shader, nullptr, nullptr);

#if 0
        // Save pdb.
        IDxcBlob* pPDB = nullptr;
        IDxcBlobUtf16* pPDBName = nullptr;
        pResults->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pPDB), &pPDBName);
        {
            FILE* fp = NULL;
            _wfopen_s(&fp, pPDBName->GetStringPointer(), L"wb");
            fwrite(pPDB->GetBufferPointer(), pPDB->GetBufferSize(), 1, fp);
            fclose(fp);
        }
#endif

        return shaderBytecode;
    }

    D3D12_SHADER_BYTECODE CreateShaderByteCode(Shader* shader, IDxcResult* pResults)
    {
        // Save shader binary.
        IDxcBlob* pShader = nullptr;
        IDxcBlobUtf16* pShaderName = nullptr;
        pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), &pShaderName);
        if (pShader == nullptr)
        {
            return D3D12_SHADER_BYTECODE{};
        }
        // fill out shader bytecode structure for pixel shader
        D3D12_SHADER_BYTECODE shaderBytecode = {};
        shaderBytecode.BytecodeLength = pShader->GetBufferSize();
        shaderBytecode.pShaderBytecode = pShader->GetBufferPointer();

        return shaderBytecode;
    }

    void CreateRootSignature(Shader* shader, IDxcResult* pResults)
    {
        IDxcBlob* pSig = nullptr;
        IDxcBlobUtf16* pSigName = nullptr;
        pResults->GetOutput(DXC_OUT_ROOT_SIGNATURE, IID_PPV_ARGS(&pSig), &pSigName);
        if (pSig != nullptr)
        {
            auto hr = GPU::instance->device->CreateRootSignature(0, pSig->GetBufferPointer(), pSig->GetBufferSize(), IID_PPV_ARGS(&shader->rootSignature));
            if (!SUCCEEDED(hr))
            {
                seedAssert(false);
            }
        }
        else
        {
            IDxcBlob* pShader = nullptr;
            IDxcBlobUtf16* pShaderName = nullptr;
            pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), &pShaderName);
            auto hr = GPU::instance->device->CreateRootSignature(1, pShader->GetBufferPointer(), pShader->GetBufferSize(), IID_PPV_ARGS(&shader->rootSignature));
            if (!SUCCEEDED(hr))
            {
                seedAssert(false);
            }
        }
    }

    void CreateCommandSignature(Shader* shader)
    {
        // Create the command signature used for indirect drawing.
        // Each command consists of a CBV update and a DrawInstanced call.
        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[3] = {};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDescs[0].Constant.RootParameterIndex = 4;
        argumentDescs[0].Constant.Num32BitValuesToSet = 1;
        argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDescs[1].Constant.RootParameterIndex = 5;
        argumentDescs[1].Constant.Num32BitValuesToSet = 1;
        argumentDescs[1].Constant.DestOffsetIn32BitValues = 0;
        if (shader->type == Shader::Type::Graphic)
            argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        else if (shader->type == Shader::Type::Compute)
            argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        else if (shader->type == Shader::Type::Raytracing)
            argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS;


        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
        commandSignatureDesc.pArgumentDescs = argumentDescs;
        commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
        if (shader->type == Shader::Type::Graphic)
            commandSignatureDesc.ByteStride = sizeof(HLSL::MeshletDrawCall);
        else if (shader->type == Shader::Type::Compute)
            commandSignatureDesc.ByteStride = sizeof(HLSL::InstanceCullingDispatch);
        else if (shader->type == Shader::Type::Raytracing)
            commandSignatureDesc.ByteStride = sizeof(HLSL::RayDispatch);

        auto hr = GPU::instance->device->CreateCommandSignature(&commandSignatureDesc, shader->rootSignature, IID_PPV_ARGS(&shader->commandSignature));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            seedAssert(false);
        }
    }

    void CreateRTShaderLibrary(Shader* shader, IDxcResult* pResults)
    {
        if (shader->type != Shader::Type::Raytracing)
            return;

        // https://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.5.pdf

        IDxcBlob* rtLibrary;
        pResults->GetResult(&rtLibrary);

        shader->rayGen.push_back("RayGen");
        shader->miss.push_back("Miss");
        shader->hit.push_back("ClosestHit");
        shader->hit.push_back("AnyHit");
        shader->hitGroup.push_back("HitGroup");

        D3D12_EXPORT_DESC exports[] = { 
            {L"RayGen", 0, D3D12_EXPORT_FLAG_NONE}, 
            {L"Miss", 0, D3D12_EXPORT_FLAG_NONE},
            {L"ClosestHit", 0, D3D12_EXPORT_FLAG_NONE},
            {L"AnyHit", 0, D3D12_EXPORT_FLAG_NONE} };

        D3D12_DXIL_LIBRARY_DESC libDesc;
        libDesc.DXILLibrary.BytecodeLength = rtLibrary->GetBufferSize();
        libDesc.DXILLibrary.pShaderBytecode = rtLibrary->GetBufferPointer();
        libDesc.NumExports = ARRAYSIZE(exports);
        libDesc.pExports = exports;

        D3D12_HIT_GROUP_DESC hitGroups[] = { {L"HitGroup", D3D12_HIT_GROUP_TYPE_TRIANGLES, L"AnyHit", L"ClosestHit", nullptr /*intersection name*/}};

        D3D12_RAYTRACING_SHADER_CONFIG shaderDesc = {};
        shaderDesc.MaxPayloadSizeInBytes = sizeof(HLSL::HitInfo);
        shaderDesc.MaxAttributeSizeInBytes = 8;

        const WCHAR* exportSymboles[] = { L"RayGen", L"Miss", L"HitGroup", L"AnyHit", L"ClosestHit" }; // all synboles from lib + hit group name + hit shader name (must be unique, may be created from a std::set)


        // The pipeline is made of a set of sub-objects, representing the DXIL libraries, hit group
        // declarations, root signature associations, plus some configuration objects
        UINT64 subobjectCount =
            1 + //ARRAYSIZE(exports) +                     // DXIL libraries
            ARRAYSIZE(hitGroups) +                                      // Hit group declarations
            1 +                                      // Shader configuration
            1 +                                      // Shader payload
            //2 * m_rootSignatureAssociations.size() + // Root signature declaration + association
            1 +                                      // Empty global root signatures <- real root sig !!
            1 +                                      // Empty local root signatures
            1;                                       // Final pipeline subobject

        // Initialize a vector with the target object count. It is necessary to make the allocation before
        // adding subobjects as some subobjects reference other subobjects by pointer. Using push_back may
        // reallocate the array and invalidate those pointers.
        std::vector<D3D12_STATE_SUBOBJECT> subobjects(subobjectCount);

        UINT currentIndex = 0;

        // Add all the DXIL libraries
        //for (uint i = 0; i < ARRAYSIZE(exports); i++)
        {
            D3D12_STATE_SUBOBJECT libSubobject = {};
            libSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            libSubobject.pDesc = &libDesc;
            subobjects[currentIndex++] = libSubobject;
        }

        // Add all the hit group declarations
        for (uint i = 0; i < ARRAYSIZE(hitGroups); i++)
        {
            D3D12_STATE_SUBOBJECT hitGroup = {};
            hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            hitGroup.pDesc = &hitGroups[i];
            subobjects[currentIndex++] = hitGroup;
        }

        // Add a subobject for the shader payload configuration
        D3D12_STATE_SUBOBJECT shaderConfigObject = {};
        shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderConfigObject.pDesc = &shaderDesc;
        subobjects[currentIndex++] = shaderConfigObject;

        // Add a subobject for the association between shaders and the payload
        // Associate the set of shaders with the payload defined in the previous subobject
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderPayloadAssociation = {};
        shaderPayloadAssociation.NumExports = ARRAYSIZE(exportSymboles);
        shaderPayloadAssociation.pExports = exportSymboles;
        shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[(currentIndex - 1)];

        // Create and store the payload association object
        D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
        shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;
        subobjects[currentIndex++] = shaderPayloadAssociationObject;

        // The pipeline construction always requires an empty global root signature <- NOT A DUMMY ! I have a real global rootsig
        D3D12_STATE_SUBOBJECT globalRootSig;
        globalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        ID3D12RootSignature* dgSig = shader->rootSignature;
        globalRootSig.pDesc = &dgSig;

        subobjects[currentIndex++] = globalRootSig;

#if 0 // no local sigroot
        // The root signature association requires two objects for each: one to declare the root
        // signature, and another to associate that root signature to a set of symbols
        for (RootSignatureAssociation& assoc : m_rootSignatureAssociations)
        {
            // Add a subobject to declare the root signature
            D3D12_STATE_SUBOBJECT rootSigObject = {};
            rootSigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            rootSigObject.pDesc = &assoc.m_rootSignature;

            subobjects[currentIndex++] = rootSigObject;

            // Add a subobject for the association between the exported shader symbols and the root
            // signature
            assoc.m_association.NumExports = static_cast<UINT>(assoc.m_symbolPointers.size());
            assoc.m_association.pExports = assoc.m_symbolPointers.data();
            assoc.m_association.pSubobjectToAssociate = &subobjects[(currentIndex - 1)];

            D3D12_STATE_SUBOBJECT rootSigAssociationObject = {};
            rootSigAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            rootSigAssociationObject.pDesc = &assoc.m_association;

            subobjects[currentIndex++] = rootSigAssociationObject;
        }

#endif
        // The pipeline construction always requires an empty local root signature
        D3D12_STATE_SUBOBJECT dummyLocalRootSig;
        dummyLocalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
        ID3D12RootSignature* dlSig = CreateDummyRootSignatures();
        dummyLocalRootSig.pDesc = &dlSig;
        subobjects[currentIndex++] = dummyLocalRootSig;

        // Add a subobject for the ray tracing pipeline configuration
        D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
        pipelineConfig.MaxTraceRecursionDepth = 3;

        D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
        pipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        pipelineConfigObject.pDesc = &pipelineConfig;

        subobjects[currentIndex++] = pipelineConfigObject;

        // Describe the ray tracing pipeline state object
        D3D12_STATE_OBJECT_DESC pipelineDesc = {};
        pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        pipelineDesc.NumSubobjects = currentIndex; // static_cast<UINT>(subobjects.size());
        pipelineDesc.pSubobjects = subobjects.data();

        // Create the state object
        HRESULT hr = GPU::instance->device->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&shader->rtStateObject));
        if (FAILED(hr))
        {
            //throw std::logic_error("Could not create the raytracing state object");
            std::cout << " !! Could not create the raytracing state object !! \n";
            assert(true);
        }

        shader->rtStateObject->QueryInterface(&shader->rtStateObjectProps);
    }
    ID3D12RootSignature* CreateDummyRootSignatures()
    {
        // Creation of the global root signature
        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.NumParameters = 0;
        rootDesc.pParameters = nullptr;
        // A global root signature is the default, hence this flag
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        HRESULT hr = 0;

        ID3DBlob* serializedRootSignature;
        ID3DBlob* error;
        ID3D12RootSignature* m_dummyLocalRootSignature;

        // Create the local root signature, reusing the same descriptor but altering the creation flag
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSignature, &error);
        if (FAILED(hr))
        {
            //throw std::logic_error("Could not serialize the local root signature");
        }
        hr = GPU::instance->device->CreateRootSignature(0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS(&m_dummyLocalRootSignature));

        serializedRootSignature->Release();
        if (FAILED(hr))
        {
            //throw std::logic_error("Could not create the local root signature");
        }
        return m_dummyLocalRootSignature;
    }

    ID3D12PipelineState* CreatePSO(PipelineStateStream& stream)
    {
        ID3D12PipelineState* pso = nullptr;
        D3D12_SHADER_BYTECODE& vs = stream.VS;
        D3D12_SHADER_BYTECODE& ms = stream.MS;
        D3D12_SHADER_BYTECODE& cs = stream.CS;
        if (vs.pShaderBytecode != nullptr || ms.pShaderBytecode != nullptr || cs.pShaderBytecode != nullptr)
        {
            D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &stream };
            HRESULT hr = GPU::instance->device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pso));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
                pso = nullptr;
            }
        }

        return pso;
    }

    void ShaderReflection(IDxcResult* pResults, Shader* shader = NULL, std::vector<D3D12_INPUT_ELEMENT_DESC>* inputLayoutElements = NULL, std::vector<DXGI_FORMAT>* outputLayoutElements = NULL)
    {
        // Reflection to get custom cbuffer layout
        IDxcBlob* pReflectionData;
        pResults->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&pReflectionData), nullptr);
        DxcBuffer reflectionBuffer;
        reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
        reflectionBuffer.Size = pReflectionData->GetBufferSize();
        reflectionBuffer.Encoding = 0;
        ID3D12ShaderReflection* pShaderReflection;
        HRESULT hr = DxcUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&pShaderReflection));
        if (FAILED(hr))
        {
            GPU::PrintDeviceRemovedReason(hr);
            return;
        }

        D3D12_SHADER_DESC refDesc;
        pShaderReflection->GetDesc(&refDesc);

        std::cout << "InstructionCounts (float | int | texture | total): " << refDesc.FloatInstructionCount << " | " << refDesc.IntInstructionCount << " | " << refDesc.TextureLoadInstructions << " | " << refDesc.InstructionCount << " -- TempRegisterCount : " << refDesc.TempRegisterCount << std::endl;
        /*
        if (shader != NULL)
        {
            for (uint j = 0; j < refDesc.ConstantBuffers; j++)
            {
                ID3D12ShaderReflectionConstantBuffer* resource = pShaderReflection->GetConstantBufferByIndex(j);
                D3D12_SHADER_BUFFER_DESC bufDesc;
                resource->GetDesc(&bufDesc);
                if (strcmp(bufDesc.Name, "materials[0]") == 0)
                {
                    for (uint k = 0; k < bufDesc.Variables; k++)
                    {
                        ID3D12ShaderReflectionVariable* matVar = resource->GetVariableByIndex(k);
                        D3D12_SHADER_VARIABLE_DESC matDesc = {};
                        matVar->GetDesc(&matDesc);
                        D3D12_SHADER_TYPE_DESC matTypeDesc;
                        ID3D12ShaderReflectionType* matType = matVar->GetType();
                        matType->GetDesc(&matTypeDesc);



                        //std::cout << "\n struct Material \n {";
                        for (uint l = 0; l < matTypeDesc.Members; l++)
                        {
                            LPCSTR memberName = matType->GetMemberTypeName(l);
                            ID3D12ShaderReflectionType* memberType = matType->GetMemberTypeByIndex(l);
                            D3D12_SHADER_TYPE_DESC memberTypeDesc;
                            memberType->GetDesc(&memberTypeDesc);

                            shader->propertiesMemoryPointers[memberName] = memberTypeDesc;

                            //std::cout << "\n     " << memberName << " " << memberTypeDesc.Type;
                        }
                        //std::cout << "\n }";

                    }
                }
            }

            // Create imput Layout
            if (inputLayoutElements != NULL)
            {
                // Read input layout description from shader info
                for (uint i = 0; i < refDesc.InputParameters; i++)
                {
                    D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
                    pShaderReflection->GetInputParameterDesc(i, &paramDesc);

                    if (strncmp("SV_", paramDesc.SemanticName, 3) == 0)
                    {
                        continue;
                    }

                    // fill out input element desc
                    D3D12_INPUT_ELEMENT_DESC elementDesc;
                    elementDesc.SemanticName = paramDesc.SemanticName;
                    elementDesc.SemanticIndex = paramDesc.SemanticIndex;
                    elementDesc.InputSlot = i < VERTICE_BUFFERS_COUNT ? i : VERTICE_BUFFERS_COUNT - 1;
                    elementDesc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
                    elementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                    elementDesc.InstanceDataStepRate = 0;

                    // determine DXGI format
                    if (paramDesc.Mask == 1)
                    {
                        if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32_UINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32_SINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32_FLOAT;
                    }
                    else if (paramDesc.Mask <= 3)
                    {
                        if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
                    }
                    else if (paramDesc.Mask <= 7)
                    {
                        if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                    }
                    else if (paramDesc.Mask <= 15)
                    {
                        if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                    }

                    //save element desc
                    inputLayoutElements->push_back(elementDesc);
                }
            }

            // Create imput Layout
            if (outputLayoutElements != NULL)
            {
                // Read input layout description from shader info
                for (uint i = 0; i < refDesc.OutputParameters; i++)
                {
                    D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
                    pShaderReflection->GetOutputParameterDesc(i, &paramDesc);

                    // Create the SO Declaration
                    D3D12_SO_DECLARATION_ENTRY entry;
                    entry.SemanticIndex = paramDesc.SemanticIndex;
                    entry.SemanticName = paramDesc.SemanticName;
                    entry.Stream = paramDesc.Stream;
                    entry.StartComponent = 0; // Assume starting at 0
                    entry.OutputSlot = 0; // Assume the first output slot

                    DXGI_FORMAT format;
                    // determine DXGI format
                    if (paramDesc.Mask == 1)
                    {
                        if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) format = DXGI_FORMAT_R32_UINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) format = DXGI_FORMAT_R32_SINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) format = DXGI_FORMAT_R32_FLOAT;
                    }
                    else if (paramDesc.Mask <= 3)
                    {
                        if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) format = DXGI_FORMAT_R32G32_UINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) format = DXGI_FORMAT_R32G32_SINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) format = DXGI_FORMAT_R32G32_FLOAT;
                    }
                    else if (paramDesc.Mask <= 7)
                    {
                        if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) format = DXGI_FORMAT_R32G32B32_UINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) format = DXGI_FORMAT_R32G32B32_SINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) format = DXGI_FORMAT_R32G32B32_FLOAT;
                    }
                    else if (paramDesc.Mask <= 15)
                    {
                        if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) format = DXGI_FORMAT_R32G32B32A32_UINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) format = DXGI_FORMAT_R32G32B32A32_SINT;
                        else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                    }

                    outputLayoutElements->push_back(format);
                }
            }
        }
        */

        if (shader != NULL)
        {
            uint x;
            uint y;
            uint z;
            pShaderReflection->GetThreadGroupSize(&x, &y, &z);
            shader->numthreads = uint3(x, y, z);
        }
    }

    bool Parse(Shader& shader, String file)
    {
		ZoneScoped;
        bool compiled = false;
        String ps = file;
        struct stat result;
        if (stat(ps.c_str(), &result) == 0 && shader.creationTime.find(file) == shader.creationTime.end())
        {
            shader.creationTime[file] = result.st_mtime;
        }

		String line;
		std::ifstream myfile(ps);
		if (myfile.is_open())
		{
			while (getline(myfile, line))
			{
				if (line.find("#include") != -1)
				{
					size_t index = line.find("\"");
					if (index != -1)
					{
						// open include file
						String includeFile = String(("src\\Shaders\\" + line.substr(index + 1, line.find_last_of("\"") - 1 - index)).c_str());
                        compiled = Parse(shader, includeFile);
					}
				}

				if (line._Starts_with("#pragma "))
				{
					auto tokens = line.Split(" ");

					// add empty strings for shaders passes names to avoid checking if a token is there
					for (uint i = (uint)tokens.size(); i < 5; i++)
					{
						tokens.push_back(" ");
					}
					/*
					#pragma gBuffer meshMain pixelBuffers
					#pragma zPrepass vertexDepth
					#pragma forward meshMain pixelMain
					*/
					if (tokens[1] == "gBuffer")
					{
                        shader.type = Shader::Type::Graphic;
                        D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[2], "ms_6_6", &shader);
                        D3D12_SHADER_BYTECODE bufferShaderBytecode = Compile(file, tokens[3], "ps_6_6");
                        PipelineStateStream stream{}; 
                        DXGI_FORMAT targetFormat[] = {DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16_FLOAT};
                        D3D12_RT_FORMAT_ARRAY RTVFormats = {};
                        for (uint i = 0; i < ARRAYSIZE(targetFormat); i++)
                        {
                            RTVFormats.RTFormats[i] = targetFormat[i];
                        }
                        for (uint i = ARRAYSIZE(targetFormat); i < 8; i++)
                        {
                            RTVFormats.RTFormats[i] = DXGI_FORMAT_UNKNOWN;
                        }
                        stream.RTFormats = RTVFormats;
                        stream.MS = meshShaderBytecode;
                        stream.PS = bufferShaderBytecode;
                        shader.pso = CreatePSO(stream);
                        compiled = shader.pso != nullptr;
					}
					else if (tokens[1] == "zPrepass")
					{
                        shader.type = Shader::Type::Graphic;
                        D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[2], "ms_6_6", &shader);
                        PipelineStateStream stream{};
                        stream.MS = meshShaderBytecode;
                        shader.pso = CreatePSO(stream);
                        compiled = shader.pso != nullptr;
					}
                    else if (tokens[1] == "forward")
                    {
                        shader.type = Shader::Type::Graphic;
                        //D3D12_SHADER_BYTECODE amplificationShaderBytecode = Compile(file, tokens[2], "as_6_6", &shader);
                        D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[3], "ms_6_6", &shader);
                        D3D12_SHADER_BYTECODE forwardShaderBytecode = Compile(file, tokens[4], "ps_6_6");
                        PipelineStateStream stream;
                        //stream.AS = amplificationShaderBytecode;
                        stream.MS = meshShaderBytecode;
                        stream.PS = forwardShaderBytecode;
                        stream.pRootSignature = shader.rootSignature;
                        shader.pso = nullptr;
                        shader.pso = CreatePSO(stream);
                        compiled = shader.pso != nullptr;
                    }
                    else if (tokens[1] == "compute")
                    {
                        shader.type = Shader::Type::Compute;
                        D3D12_SHADER_BYTECODE computeShaderBytecode = Compile(file, tokens[2], "cs_6_6", &shader);
                        PipelineStateStream stream;
                        stream.CS = computeShaderBytecode;
                        stream.pRootSignature = shader.rootSignature;
                        shader.pso = nullptr;
                        shader.pso = CreatePSO(stream);
                        compiled = shader.pso != nullptr;
                    }
                    else if (tokens[1] == "raytracing")
                    {
                        shader.type = Shader::Type::Raytracing;
                        D3D12_SHADER_BYTECODE computeShaderBytecode = Compile(file, "", "lib_6_6", &shader);
                        PipelineStateStream stream = {};
                        stream.pRootSignature = shader.rootSignature;
                        shader.pso = nullptr;
                        //shader.pso = CreatePSO(stream);
                        //compiled = shader.pso != nullptr;
                        compiled = true;
                    }
				}
			}
		}
        return compiled;
    }

    bool Load(Shader& shader, String file)
    {
        bool compiled = Parse(shader, file);
        if (compiled)
            IOs::Log("compiled {}", file.c_str());
        return compiled;
    }
};
ShaderLoader* ShaderLoader::instance = nullptr;


inline void AssetLibrary::LoadAssets()
{
    //ZoneScoped;
    meshLoaded = 0;
    shaderLoaded = 0;
    textureLoaded = 0;

    for (auto& item : loadingRequest)
    {
        assetID id = item.first;
        
        LoadAsset(id, false);

        if (meshLoaded > meshLoadingLimit && shaderLoaded > shaderLoadingLimit && textureLoaded > textureLoadingLimit)
            break;
    }
    loadingRequest.clear();
    CheckReload();
}
inline void AssetLibrary::LoadAsset(assetID id, bool ignoreBudget)
{
    switch (map[id].type)
    {
    case AssetLibrary::AssetType::mesh:
    {
        if (ignoreBudget || meshLoaded < meshLoadingLimit)
        {
            MeshData meshData = MeshLoader::instance->Read(map[id].path);
            if (meshData.meshlets.size() > 0)
            {
                Mesh mesh = MeshStorage::instance->Load(meshData, commandBuffer.Get());
                lock.lock();
                meshes.push_back(mesh);
                map[id].indexInVector = meshes.size() - 1;
                meshLoaded++;
                lock.unlock();
            }
        }
    }
    break;
    case AssetLibrary::AssetType::shader:
    {
        if (ignoreBudget || shaderLoaded < shaderLoadingLimit)
        {
            Shader shader;
            bool compiled = ShaderLoader::instance->Load(shader, map[id].path);
            if (compiled)
            {
                lock.lock();
                uint index = map[id].indexInVector;
                if (index != ~0)
                {
                    shaders[index] = shader;
                }
                else
                {
                    shaders.push_back(shader);
                    map[id].indexInVector = shaders.size() - 1;
                }
                shaderLoaded++;
                lock.unlock();
            }
        }
    }
    break;
    case AssetLibrary::AssetType::texture:
    {
        if (ignoreBudget || textureLoaded < textureLoadingLimit)
        {
            Resource texture;
            bool loaded = TextureLoader::instance->Load(texture, map[id].path);
            if (loaded)
            {
                lock.lock();
                textures.push_back(texture);
                map[id].indexInVector = textures.size() - 1;
                textureLoaded++;
                lock.unlock();
            }
        }
    }
    break;
    default:
        seedAssert(false);
        break;
    }
}
