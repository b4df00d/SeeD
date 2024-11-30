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
    IDxcUtils* DxcUtils{};
    IDxcCompiler3* DxcCompiler{};

    void On()
    {
        ZoneScoped;
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
	}

    void Compile(Shader& shader, String file)
    {
		ZoneScoped;
    }


    void Load(Shader& shader, String file)
    {
		ZoneScoped;
        String ps = file;
        struct stat result;
        if (stat(ps.ToConstChar(), &result) == 0 && shader.creationTime.find(file) == shader.creationTime.end())
        {
            shader.creationTime[file] = result.st_mtime;
        }

		String line;
		std::wifstream myfile(ps);
		if (myfile.is_open())
		{
			while (getline(myfile, line))
			{
				if (line.find(L"#include") != -1)
				{
					size_t index = line.find(L"\"");
					if (index != -1)
					{
						// open include file
						String includeFile = String((L"Shaders\\" + line.substr(index + 1, line.find_last_of(L"\"") - 1 - index)).c_str());
						Load(shader, includeFile);
					}
				}

				if (line._Starts_with(L"#pragma "))
				{
					auto tokens = line.Split(L" ");

					// add empty strings for shaders passes names to avoid checking if a token is there
					for (uint i = (uint)tokens.size(); i < 4; i++)
					{
						tokens.push_back(L" ");
					}
					/*
					#pragma gBuffer meshMain pixelBuffers
					#pragma zPrepass vertexDepth
					#pragma transparent meshMain pixelMain
					*/
					if (tokens[1] == L"gBuffer")
					{
						Compile(shader, file);
					}
					else if (tokens[1] == L"zPrepass")
					{
                        Compile(shader, file);
					}
                    else if (tokens[1] == L"transparent")
                    {
                        Compile(shader, file);
                    }
				}
			}
		}
    }
};