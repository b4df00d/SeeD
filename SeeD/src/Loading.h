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
    int meshLoadingLimit = 3;
    int shaderLoadingLimit = 3;
    int textureLoadingLimit = 3;
    int meshLoaded;
    int shaderLoaded;
    int textureLoaded;

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
            commandBuffer.Get(i).On(GPU::instance->copyQueue, name);
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
        ID3D12CommandQueue* commandQueue = commandBuffer->queue;
        commandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&commandBuffer->cmd);
        commandQueue->Signal(commandBuffer->passEnd.fence, ++commandBuffer->passEnd.fenceValue);
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
            fout.write((char*)&mesh.boundingSphere, sizeof(mesh.boundingSphere));
            fout.close();
        }

        return AssetLibrary::instance->Add(path);
    }

    // also DirectXMesh can do meshlets https://github.com/microsoft/DirectXMesh
    MeshData Process(MeshOriginal& originalMesh)
    {
		ZoneScoped;
        const size_t max_vertices = 64;
        const size_t max_triangles = 124;
        const float cone_weight = 0.5f;

        size_t max_meshlets = meshopt_buildMeshletsBound(originalMesh.indices.size(), max_vertices, max_triangles);
        std::vector<meshopt_Meshlet> meshopt_meshlets(max_meshlets);
        std::vector<unsigned int> meshlet_vertices(max_meshlets * max_vertices);
        std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);

        size_t meshlet_count = meshopt_buildMeshlets(meshopt_meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), originalMesh.indices.data(), originalMesh.indices.size(), &originalMesh.vertices[0].px, originalMesh.vertices.size(), sizeof(Vertex), max_vertices, max_triangles, cone_weight);


        std::vector<Meshlet> meshlets(meshopt_meshlets.size());
        meshlets.resize(meshopt_meshlets.size());
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
        optimizedMesh.meshlet_triangles.resize(meshlet_triangles.size());
        for (uint i = 0; i < meshlet_triangles.size(); i++)
        {
            optimizedMesh.meshlet_triangles[i] = meshlet_triangles[i];
        }
        optimizedMesh.vertices = originalMesh.vertices;
        optimizedMesh.boundingSphere = originalMesh.boundingSphere;

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
            | aiProcess_OptimizeGraph
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

        for (uint i = 0; i < 100; i++)
        {
            _scene->mRootNode->mTransformation.a4 = Rand01() * 5.0f;
            _scene->mRootNode->mTransformation.b4 = Rand01() * 5.0f;
            _scene->mRootNode->mTransformation.c4 = Rand01() * 5.0f;

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
            IOs::Log("    {}", m->mName.C_Str());
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
                assetID id = MeshLoader::instance->Write(mesh, m->mName.C_Str());

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
#ifdef REVERSE_Z
        vArgs.push_back(L"-D");
        vArgs.push_back(L"REVERSE_Z");
#endif

        // Compile it with specified arguments.
        IDxcResult* pResults;
        DxcCompiler->Compile(
            &Source,
            vArgs.data(),
            (UINT32)vArgs.size(),
            pIncludeHandler,
            IID_PPV_ARGS(&pResults)
        );

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

        // Save shader binary.
        IDxcBlob* pShader = nullptr;
        IDxcBlobUtf16* pShaderName = nullptr;
        pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), &pShaderName);
        if (pShader == nullptr)
        {
            return D3D12_SHADER_BYTECODE{};
        }
        // fill out shader bytecode structure for pixel shader
        shaderBytecode = {};
        shaderBytecode.BytecodeLength = pShader->GetBufferSize();
        shaderBytecode.pShaderBytecode = pShader->GetBufferPointer();

        if (shader != nullptr)
        {
            IDxcBlob* sig = nullptr;
            pResults->GetOutput(DXC_OUT_ROOT_SIGNATURE, IID_PPV_ARGS(&sig), &pShaderName);
            if (sig == nullptr)
            {
                return D3D12_SHADER_BYTECODE{};
            }

            hr = GPU::instance->device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&shader->rootSignature));
            if (!SUCCEEDED(hr))
            {
                return D3D12_SHADER_BYTECODE{};
            }
        
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
            if(type == "ms_6_6")
                argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
            if (type == "cs_6_6")
                argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;


            D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
            commandSignatureDesc.pArgumentDescs = argumentDescs;
            commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
            commandSignatureDesc.ByteStride = sizeof(HLSL::MeshletDrawCall);

            auto hr = GPU::instance->device->CreateCommandSignature(&commandSignatureDesc, shader->rootSignature, IID_PPV_ARGS(&shader->commandSignature));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
                return D3D12_SHADER_BYTECODE{};
            }
        }

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

        ShaderReflection(pResults, shader, nullptr, nullptr);

        return shaderBytecode;
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
        DxcUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&pShaderReflection));

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
					for (uint i = (uint)tokens.size(); i < 4; i++)
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
                        D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[2], "ms_6_6", &shader);
                        D3D12_SHADER_BYTECODE bufferShaderBytecode = Compile(file, tokens[3], "ps_6_6");
                        PipelineStateStream stream{};
                        stream.MS = meshShaderBytecode;
                        stream.PS = bufferShaderBytecode;
                        shader.pso = CreatePSO(stream);
                        compiled = shader.pso != nullptr;
					}
					else if (tokens[1] == "zPrepass")
					{
                        D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[2], "ms_6_6", &shader);
                        PipelineStateStream stream{};
                        stream.MS = meshShaderBytecode;
                        shader.pso = CreatePSO(stream);
                        compiled = shader.pso != nullptr;
					}
                    else if (tokens[1] == "forward")
                    {
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
                        D3D12_SHADER_BYTECODE computeShaderBytecode = Compile(file, tokens[2], "cs_6_6", &shader);
                        PipelineStateStream stream;
                        stream.CS = computeShaderBytecode;
                        stream.pRootSignature = shader.rootSignature;
                        shader.pso = nullptr;
                        shader.pso = CreatePSO(stream);
                        compiled = shader.pso != nullptr;
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
    meshLoaded = meshLoadingLimit;
    shaderLoaded = shaderLoadingLimit;
    textureLoaded = textureLoadingLimit;

    for (auto& item : loadingRequest)
    {
        assetID id = item.first;
        
        LoadAsset(id, false);

        if (meshLoaded <= 0 && shaderLoaded <= 0 && textureLoaded <= 0)
            break;
    }
    loadingRequest.clear();
}
inline void AssetLibrary::LoadAsset(assetID id, bool ignoreBudget)
{
    switch (map[id].type)
    {
    case AssetLibrary::AssetType::mesh:
    {
        if (ignoreBudget || meshLoaded >= 0)
        {
            MeshData meshData = MeshLoader::instance->Read(map[id].path);
            if (meshData.meshlets.size() > 0)
            {
                Mesh mesh = GlobalResources::instance->meshStorage.Load(meshData, commandBuffer.Get());
                lock.lock();
                meshes.push_back(mesh);
                map[id].indexInVector = meshes.size() - 1;
                meshLoaded--;
                lock.unlock();
            }
        }
    }
    break;
    case AssetLibrary::AssetType::shader:
    {
        if (ignoreBudget || shaderLoaded >= 0)
        {
            Shader shader;
            bool compiled = ShaderLoader::instance->Load(shader, map[id].path);
            if (compiled)
            {
                lock.lock();
                shaders.push_back(shader);
                map[id].indexInVector = shaders.size() - 1;
                shaderLoaded--;
                lock.unlock();
            }
        }
    }
    break;
    case AssetLibrary::AssetType::texture:
    {
        if (ignoreBudget || textureLoaded >= 0)
        {
            Resource texture;
            bool loaded = TextureLoader::instance->Load(texture, map[id].path);
            if (loaded)
            {
                lock.lock();
                textures.push_back(texture);
                map[id].indexInVector = textures.size() - 1;
                textureLoaded--;
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
