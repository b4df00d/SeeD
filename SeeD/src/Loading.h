#pragma once

#include <wincodec.h>
#include "../../Third/DirectXTex-main/WICTextureLoader/WICTextureLoader12.h"
#include "../../Third/DirectXTex-main/DDSTextureLoader/DDSTextureLoader12.h"
class TextureLoader
{
public:
    IWICImagingFactory* wicFactory = NULL;
    void On()
    {
		ZoneScoped;
        HRESULT hr;
        CoInitialize(NULL);// Initialize the COM library
        hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));// create the WIC factory
    }

    void Off()
    {
		ZoneScoped;
        wicFactory->Release();
    }
};


#include "../../Third/meshoptimizer-master/src/meshoptimizer.h"
class MeshLoader
{
public:
    static MeshLoader* instance;
    struct Vertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
    };

    struct MeshOriginal
    {
        std::vector<uint> indices;
        std::vector<Vertex> vertices;
    };

    struct Meshlet // same as meshoptimizer meshlet
    {
        /* offsets within meshlet_vertices and meshlet_triangles arrays with meshlet data */
        unsigned int vertex_offset;
        unsigned int triangle_offset;

        /* number of vertices and triangles used in the meshlet; data is stored in consecutive range defined by offset and count */
        unsigned int vertex_count;
        unsigned int triangle_count;
    };

    struct MeshData
    {
        std::vector<Meshlet> meshlets;
        std::vector<unsigned int> meshlet_vertices;
        std::vector<unsigned int> meshlet_triangles;
        std::vector<Vertex> vertices;
    };

    struct Mesh
    {
        uint meshletOffset;
        uint meshletCount;
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

        const meshopt_Meshlet& last = meshopt_meshlets[meshlet_count - 1];

        meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
        meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
        meshopt_meshlets.resize(meshlet_count);

        std::vector<Meshlet> meshlets(meshopt_meshlets.size());
        meshlets.resize(meshopt_meshlets.size());
        for (uint i = 0; i < meshlet_count; i++)
        {
            auto& m = meshopt_meshlets[i];
            meshopt_optimizeMeshlet(&meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset], m.triangle_count, m.vertex_count);
            meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset], m.triangle_count, &originalMesh.vertices[0].px, originalMesh.vertices.size(), sizeof(Vertex));

            meshlets[i] = *(Meshlet*)&m;
        }

        MeshData optimizedMesh;
        optimizedMesh.meshlets = meshlets;
        optimizedMesh.meshlet_vertices = meshlet_vertices;
        optimizedMesh.meshlet_triangles.resize(meshlet_triangles.size());
        for (uint i = 0; i < meshlet_triangles.size(); i++)
        {
            optimizedMesh.meshlet_triangles[i] = meshlet_triangles[i];
        }
        optimizedMesh.vertices = originalMesh.vertices;

        return optimizedMesh;

        //use if (dot(normalize(cone_apex - camera_position), cone_axis) >= cone_cutoff) reject(); in mesh shader for cone culling
    }
};
MeshLoader* MeshLoader::instance;

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
    void On()
    {

    }

    void Off()
    {

    }

    void Load(String path)
    {
        ZoneScoped;

        IOs::Log("Loading : {}", path.c_str());

        Assimp::Importer importer;
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, true);
        importer.SetPropertyBool(AI_CONFIG_FBX_CONVERT_TO_M, true);
        const aiScene* _scene = importer.ReadFile(path,
            // for DX
            aiProcess_MakeLeftHanded
            | aiProcess_FlipWindingOrder
            | aiProcess_FlipUVs
            
            | aiProcess_CalcTangentSpace
            | aiProcess_FixInfacingNormals
            | aiProcess_GenSmoothNormals
            //| aiProcess_GenNormals
            | aiProcess_Triangulate
            | aiProcess_JoinIdenticalVertices
            | aiProcess_SortByPType
            | aiProcess_FindInvalidData
            | aiProcess_FindInstances
            | aiProcess_GlobalScale
            | aiProcess_GenBoundingBoxes
            //| aiProcess_RemoveRedundantMaterials
            //| aiProcess_OptimizeGraph
            //| aiProcess_OptimizeMeshes

            | aiProcess_PopulateArmatureData
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
        CreateEntities(_scene, _scene->mRootNode);
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

        // if node has meshes, create a new scene object for it
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            if(parentEntity != entityInvalid)
            {
                ent.Make(Components::Instance::mask | Components::Transform::mask | Components::WorldMatrix::mask | Components::Name::mask | Components::Parent::mask);
            }
            else
            {
                ent.Make(Components::Instance::mask | Components::Transform::mask | Components::WorldMatrix::mask | Components::Name::mask);
            }

            auto& name = ent.Get<Components::Name>();
            strcpy_s(name.name, 256, node->mName.C_Str());

            auto& transform = ent.Get<Components::Transform>();
            transform.position = pos;
            transform.rotation = rot;
            transform.scale = scale;

            auto& matrix = ent.Get<Components::WorldMatrix>();
            matrix.matrix = float4x4(1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                Rand01() - 0.5f, Rand01() - 0.5f, Rand01(), 1);

            auto& parent = ent.Get<Components::Parent>();
            parent.id = { parentEntity.id };

            auto& instance = ent.Get<Components::Instance>();
            instance.mesh = Components::Handle<Components::Mesh>{ meshIndexToEntity[node->mMeshes[i]].id };
            instance.material = Components::Handle<Components::Material>{ matIndexToEntity[_scene->mMeshes[node->mMeshes[i]]->mMaterialIndex].id };
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

                originalMesh.vertices.resize(m->mNumVertices);
                for (unsigned int j = 0; j < m->mNumVertices; j++)
                {
                    MeshLoader::Vertex& v = originalMesh.vertices[j];

                    v.px = m->mVertices[j].x * 1000;
                    v.py = m->mVertices[j].y * 1000;
                    v.pz = m->mVertices[j].z * 1000;
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

                // pour le moment ca marche que pour du triangulé (le 3)
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

                MeshLoader::MeshData mesh = MeshLoader::instance->Process(originalMesh);
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

    D3D12_SHADER_BYTECODE Compile(String file, String entry, String type, ID3D12RootSignature** rootSignature = nullptr)
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
        vArgs.push_back(DXC_ARG_ALL_RESOURCES_BOUND);
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
        vArgs.push_back(L"-Qstrip_debug");
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

        if (rootSignature != nullptr)
        {
            IDxcBlob* sig = nullptr;
            pResults->GetOutput(DXC_OUT_ROOT_SIGNATURE, IID_PPV_ARGS(&sig), &pShaderName);
            if (sig == nullptr)
            {
                return D3D12_SHADER_BYTECODE{};
            }

            hr = GPU::instance->device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(rootSignature));
            if (!SUCCEEDED(hr))
            {
                return D3D12_SHADER_BYTECODE{};
            }
        }

        /*
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
        */

        return shaderBytecode;
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
                        D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[2], "ms_6_6", &shader.rootSignature);
                        D3D12_SHADER_BYTECODE bufferShaderBytecode = Compile(file, tokens[3], "ps_6_6");
                        PipelineStateStream stream{};
                        stream.MS = meshShaderBytecode;
                        stream.PS = bufferShaderBytecode;
                        shader.pso = GPU::instance->CreatePSO(stream);
                        compiled = shader.pso != nullptr;
					}
					else if (tokens[1] == "zPrepass")
					{
                        D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[2], "ms_6_6", &shader.rootSignature);
                        PipelineStateStream stream{};
                        stream.MS = meshShaderBytecode;
                        shader.pso = GPU::instance->CreatePSO(stream);
                        compiled = shader.pso != nullptr;
					}
                    else if (tokens[1] == "forward")
                    {
                        D3D12_SHADER_BYTECODE amplificationShaderBytecode = Compile(file, tokens[2], "as_6_6", &shader.rootSignature);
                        D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[3], "ms_6_6", &shader.rootSignature);
                        D3D12_SHADER_BYTECODE forwardShaderBytecode = Compile(file, tokens[4], "ps_6_6");
                        PipelineStateStream stream;
                        stream.AS = amplificationShaderBytecode;
                        stream.MS = meshShaderBytecode;
                        stream.PS = forwardShaderBytecode;
                        stream.pRootSignature = shader.rootSignature;
                        shader.pso = nullptr;
                        shader.pso = GPU::instance->CreatePSO(stream);
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