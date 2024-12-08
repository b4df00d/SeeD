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
class AssetLoader
{
public:

    struct Vertex
    {
        float x;
        float y;
        float z;
        float u;
        float v;
    };

    struct MeshOriginal
    {
        std::vector<uint> indices;
        std::vector<Vertex> vertices;
    };

    struct Mesh
    {
        size_t meshlet_count;
        std::vector<meshopt_Meshlet> meshlets;
        std::vector<unsigned int> meshlet_vertices;
        std::vector<unsigned char> meshlet_triangles;
    };

	void On()
	{
		ZoneScoped;
	}

	void Off()
	{
		ZoneScoped;
	}

    void Load(String path)
    {
		ZoneScoped;
    }

    // also DirectXMesh can do meshlets https://github.com/microsoft/DirectXMesh
    Mesh Process(MeshOriginal& originalMesh)
    {
		ZoneScoped;
        const size_t max_vertices = 64;
        const size_t max_triangles = 124;
        const float cone_weight = 0.5f;

        size_t max_meshlets = meshopt_buildMeshletsBound(originalMesh.indices.size(), max_vertices, max_triangles);
        std::vector<meshopt_Meshlet> meshlets(max_meshlets);
        std::vector<unsigned int> meshlet_vertices(max_meshlets * max_vertices);
        std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);

        size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), originalMesh.indices.data(), originalMesh.indices.size(), &originalMesh.vertices[0].x, originalMesh.vertices.size(), sizeof(Vertex), max_vertices, max_triangles, cone_weight);

        const meshopt_Meshlet& last = meshlets[meshlet_count - 1];

        meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
        meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
        meshlets.resize(meshlet_count);

        for (uint i = 0; i < meshlet_count; i++)
        {
            auto& m = meshlets[i];
            meshopt_optimizeMeshlet(&meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset], m.triangle_count, m.vertex_count);
            meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset], m.triangle_count, &originalMesh.vertices[0].x, originalMesh.vertices.size(), sizeof(Vertex));
        }

        Mesh optimizedMesh;
        optimizedMesh.meshlet_count = meshlet_count;
        optimizedMesh.meshlets = meshlets;
        optimizedMesh.meshlet_vertices = meshlet_vertices;
        optimizedMesh.meshlet_triangles = meshlet_triangles;

        return optimizedMesh;

        //use if (dot(normalize(cone_apex - camera_position), cone_axis) >= cone_cutoff) reject(); in mesh shader for cone culling
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

    bool Compile(Shader& shader, String file, String entry)
    {
		ZoneScoped;
        bool compiled = false;

        std::wstring wfile = file.ToWString();

        HRESULT hr;
        ID3DBlob* errorBuff = NULL; // a buffer holding the error data if any
        ID3DBlob* signature = NULL;

        
        D3D12_SHADER_BYTECODE meshShaderBytecode{};
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


        auto includePath = std::wstring(L"src\\Shaders\\");
        auto entryName = std::wstring(entry.ToWString());
        std::vector<LPCWSTR> vArgs;
        //vArgs.push_back(shaderFileName);
        vArgs.push_back(L"-I");
        vArgs.push_back(includePath.c_str());
        vArgs.push_back(L"-E");
        vArgs.push_back(entryName.c_str());
        vArgs.push_back(L"-T");
        vArgs.push_back(L"ms_6_8");
        vArgs.push_back(DXC_ARG_ALL_RESOURCES_BOUND);
        vArgs.push_back(L"-no-warnings");
#ifdef _DEBUG
        vArgs.push_back(L"/Zi");
        vArgs.push_back(L"/Zss");
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
            return false;
        }
        // Quit if the compilation failed.
        HRESULT hrStatus;
        pResults->GetStatus(&hrStatus);
        if (!SUCCEEDED(hrStatus))
        {
            return false;
        }

        // Save shader binary.
        IDxcBlob* pShader = nullptr;
        IDxcBlobUtf16* pShaderName = nullptr;
        pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), &pShaderName);
        if (pShader == nullptr)
        {
            return false;
        }
        // fill out shader bytecode structure for pixel shader
        meshShaderBytecode = {};
        meshShaderBytecode.BytecodeLength = pShader->GetBufferSize();
        meshShaderBytecode.pShaderBytecode = pShader->GetBufferPointer();

        IDxcBlob* sig = nullptr;
        pResults->GetOutput(DXC_OUT_ROOT_SIGNATURE, IID_PPV_ARGS(&sig), &pShaderName);
        if (sig == nullptr)
        {
            return false;
        }

        hr = GPU::instance->device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&(shader.rootSignature)));


        D3D12_SHADER_BYTECODE vertexShaderBytecode{};
        D3D12_SHADER_BYTECODE pixelShaderBytecode{};

        return compiled;
    }


    bool Load(Shader& shader, String file)
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
						String includeFile = String(("Shaders\\" + line.substr(index + 1, line.find_last_of("\"") - 1 - index)).c_str());
                        compiled = Load(shader, includeFile);
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
					#pragma transparent meshMain pixelMain
					*/
					if (tokens[1] == "gBuffer")
					{
                        compiled = Compile(shader, file, tokens[2]);
					}
					else if (tokens[1] == "zPrepass")
					{
                        compiled = Compile(shader, file, tokens[2]);
					}
                    else if (tokens[1] == "transparent")
                    {
                        compiled = Compile(shader, file, tokens[2]);
                    }
				}
			}
		}
        return compiled;
    }
};
ShaderLoader* ShaderLoader::instance = nullptr;