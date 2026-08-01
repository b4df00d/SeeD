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
        String originalFilePath;
        AssetLibrary::AssetType type;
        void* data = nullptr; // points directly into MeshStorage::allMeshes / TextureStorage::textures / ShaderStorage::shaders
        uint storageIndex = ~0u; // this asset's key in the storage above -- needed to erase it (see TextureStorage::Release)
        uint lastGetFrameCount = 0;
    };
    static AssetLibrary* instance;
    // https://www.youtube.com/watch?v=cGB3wT0U5Ao&ab_channel=CppCon
    // used Open Addressing Hash Map ?
    // plf::colony ?
    String assetsPath;
    std::unordered_map<assetID, Asset> map;
    std::vector<assetID> assetsAlive;
    String importPath;
    std::unordered_map<assetID, String> allAssetsInImportPath;
    std::unordered_map<assetID, uint> loadingRequest;
    int meshLoadingLimit = 5;
    int shaderLoadingLimit = 5;
    int textureLoadingLimit = 5;
    int unloadFrameThreshold = 600;
    int meshLoaded = 0;
    int shaderLoaded = 0;
    int textureLoaded = 0;

    PerFrame<CommandBuffer> commandBuffer;
    const char* name = "AssetLibraryUpload";

    void On()
    {
        ZoneScoped;
        instance = this;
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i).On(GPU::instance->computeQueue, name); //not copyQueue because creation of blas needs it :(
        }
        assetsPath = Project::instance->CacheDirAbs();
        importPath = Project::instance->SourceDirAbs();
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories((std::string)assetsPath, ec);
        fs::create_directories((std::string)importPath, ec);
    }

    void Off()
    {
        ZoneScoped;
        for (uint i = 0; i < FRAMEBUFFERING; i++)
        {
            commandBuffer.Get(i).Off();
        }

        map.clear();
        assetsAlive.clear();
        loadingRequest.clear();
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
        PIXBeginEvent(commandBuffer->cmd, PIX_COLOR_INDEX((BYTE)(UINT64)name), name);
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
            commandBuffer->queue->Wait(endOfLastFrame->GetPrevious().passEnd.fence, endOfLastFrame->GetPrevious().passEnd.fenceValue);
        }
        commandBuffer->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&commandBuffer->cmd);
        commandBuffer->queue->Signal(commandBuffer->passEnd.fence, ++commandBuffer->passEnd.fenceValue);
    }

    void CheckAssetsLifeTime()
    {
        ZoneScoped;
        for (uint i=0; i<assetsAlive.size(); i++)
        {
            assetID ID = assetsAlive[i];
            auto& item = map[ID];
            if (item.type == AssetLibrary::AssetType::shader)
            {
                if (Get<Shader>(ID)->NeedReload())
                {
                    LoadAsset(ID, false);
                }
            }
            else if (item.type == AssetLibrary::AssetType::mesh || item.type == AssetLibrary::AssetType::texture)
            {
                item.lastGetFrameCount++;
                if (item.lastGetFrameCount > (uint)unloadFrameThreshold)
                {
                    if (item.type == AssetLibrary::AssetType::texture)
                    {
                        TextureStorage::instance->Release(item.storageIndex);
                        item.data = nullptr;
                        item.storageIndex = ~0u;
                        assetsAlive[i] = assetsAlive.back();
                        assetsAlive.pop_back();
                        i--;
                    }
                    // Mesh eviction deferred: MeshStorage has no slot reclaim yet (only BLAS.Release).
                    // Meshes stay resident until that exists -- see plan risks.
                }
            }
        }
    }

    AssetLibrary::AssetType GetType(assetID id)
    {
        String path = GetPath(id);
        int extentionStart = (uint)path.find_last_of('.') + 1;
        String extenstion = path.substr(extentionStart);
        AssetLibrary::AssetType type;
        if (extenstion.starts_with("mesh")) type = AssetLibrary::AssetType::mesh;
        else if (extenstion.starts_with("hlsl")) type = AssetLibrary::AssetType::shader;
        else if (extenstion.starts_with("tex")) type = AssetLibrary::AssetType::texture;

        return type;
    }

    assetID Add(String path, String originalFilePath, assetID id = {})
    {
        ZoneScoped;
        if(id.hash == 0)
            id.hash = std::hash<std::string>{}(path);
        lock.lock();
        map[id].path = path;
        map[id].originalFilePath = originalFilePath;
        map[id].type = GetType(id);
        lock.unlock();
        return id;
    }

    assetID AddHardCoded(String path)
    {
        ZoneScoped;
        return Add(path, path);
    }

    String GetPath(assetID id)
    {
        seedAssert(map.contains(id));
        return map[id].path;
    }

    bool ProbeCache(assetID id)
    {
        String base = std::format("{}{}", assetsPath.c_str(), id.hash);
        String path;
        AssetType type;
        if (std::filesystem::exists((std::string)(base + ".mesh"))) { path = base + ".mesh"; type = AssetType::mesh; }
        else if (std::filesystem::exists((std::string)(base + ".tex"))) { path = base + ".tex"; type = AssetType::texture; }
        else return false;

        lock.lock();
        map[id].path = path;
        map[id].type = type;
        map[id].originalFilePath = "";
        map[id].data = nullptr;
        lock.unlock();
        return true;
    }

    template<typename T>
    T* Get(assetID id, bool immediate = false)
    {
        if (id == assetID::Invalid)
            return nullptr;

        if (!map.contains(id) && !ProbeCache(id))
            return nullptr;

        auto& asset = map[id];
        if (asset.data == nullptr)
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

        if (asset.data != nullptr)
        {
            asset.lastGetFrameCount = 0;
            return (T*)asset.data;
        }

        return nullptr;
    }

    void LoadAssets();
    void LoadAsset(assetID id, bool ignoreBudget);


    String FindInImportPath(String name)
    {
        ZoneScoped;
        size_t last = name.find_last_of("\\");
        if (last != -1)
            name = name.substr(last + 1);
        last = name.find_last_of("/");
        if (last != -1)
            name = name.substr(last + 1);
        name = name.ToLower();


        if (allAssetsInImportPath.size() == 0)
        {
            for (const auto& p : std::filesystem::recursive_directory_iterator((std::string)importPath))
            {
                if (!std::filesystem::is_directory(p))
                {
                    String pName = p.path().filename().string();
                    assetID id;
                    id.hash = std::hash<std::string>{}(pName.ToLower());
                    allAssetsInImportPath[id] = p.path().string();
                }
            }
            //return "";
        }
        //else
        {
            assetID id;
            id.hash = std::hash<std::string>{}(name);

            if (allAssetsInImportPath.contains(id))
                return allAssetsInImportPath[id];
        }
        return "";
    }
};
AssetLibrary* AssetLibrary::instance = nullptr;

#include <wincodec.h>
//#include "../../Third/DirectXTex-main/WICTextureLoader/WICTextureLoader12.h"
//#include "../../Third/DirectXTex-main/DDSTextureLoader/DDSTextureLoader12.h"
#include "../../Third/DirectXTex-main/DirectXTex/DirectXTex.h"
#include <dstorage.h>

inline bool LoadImageFromDisk(String path, DirectX::ScratchImage& imageOut, DirectX::TexMetadata* metadataOut = nullptr)
{
    ZoneScoped;
    DirectX::TexMetadata metadata;
    HRESULT hr;
    if (path.find(".dds") != -1)      hr = DirectX::LoadFromDDSFile(path.ToWString().c_str(), DirectX::DDS_FLAGS_NONE, &metadata, imageOut);
    else if (path.find(".tga") != -1) hr = DirectX::LoadFromTGAFile(path.ToWString().c_str(), DirectX::TGA_FLAGS_NONE, &metadata, imageOut);
    else                               hr = DirectX::LoadFromWICFile(path.ToWString().c_str(), DirectX::WIC_FLAGS_NONE, &metadata, imageOut);
    if (metadataOut) *metadataOut = metadata;
    return SUCCEEDED(hr);
}

class TextureLoader
{
public:
    static TextureLoader* instance;
    IWICImagingFactory* wicFactory = NULL;
    IDStorageFactory* factory;
    IDStorageCompressionCodec* compression;
    IDStorageQueue* queue;
    struct DirectStorageSampleTextureMetadataHeader
    {
        D3D12_RESOURCE_DESC resourceDesc;
        DSTORAGE_COMPRESSION_FORMAT compressionFormat;
        uint64_t resourceSizeCompressed;
        uint64_t resourceSizeUncompressed;
        int64_t resourceOffset;
        wchar_t resourceName[MAX_PATH]; // This is the name of the resource this header describes. It's just the file name.
    };

    void On()
    {
		ZoneScoped;
        instance = this;
        HRESULT hr;
        CoInitialize(NULL);// Initialize the COM library
        hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));// create the WIC factory

        hr = DStorageGetFactory(IID_PPV_ARGS(&factory));

        constexpr uint32_t DEFAULT_THREAD_COUNT = 0;
        hr = DStorageCreateCompressionCodec(DSTORAGE_COMPRESSION_FORMAT_GDEFLATE, DEFAULT_THREAD_COUNT, IID_PPV_ARGS(&compression));

        // Create a DirectStorage queue which will be used to load data into a
        // buffer on the GPU.
        DSTORAGE_QUEUE_DESC queueDesc{};
        queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
        queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
        queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        queueDesc.Device = GPU::instance->device;

        hr = factory->CreateQueue(&queueDesc, IID_PPV_ARGS(&queue));
    }

    void Off()
    {
		ZoneScoped;
        wicFactory->Release();
        queue->Release();
        compression->Release();
        factory->Release();
        instance = nullptr;
    }

    Resource Read(String path, String name)
    {
        ZoneScoped;
        Resource resource = {};

        // TODO : For a serious streaming system, should this be available in a RAM DataBase for super fast access ?
        String metaPath = path.substr(0, path.length() - 4) + ".meta";
        DirectStorageSampleTextureMetadataHeader metadata;
        std::ifstream fin(metaPath, std::ios::binary);
        if (fin.is_open())
        {
            fin.read((char*)&metadata, sizeof(metadata));
            fin.close();
        }

        resource.Create(metadata.resourceDesc, name, ResourceCategory::AssetTexture);

        IDStorageFile* fileHandle = nullptr;
        factory->OpenFile(path.ToWString().c_str(), IID_PPV_ARGS(&fileHandle));

        DSTORAGE_REQUEST req = {};
        req.Options.CompressionFormat = metadata.compressionFormat;
        req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        req.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;
        req.Source.File.Source = fileHandle;
        req.Source.File.Offset = metadata.resourceOffset;
        req.Source.File.Size = (uint)metadata.resourceSizeCompressed;
        req.Destination.Texture.Resource = resource.GetResource();
        req.Destination.MultipleSubresources.Resource = resource.GetResource();
        req.Destination.MultipleSubresources.FirstSubresource = 0;
        req.UncompressedSize = (uint)metadata.resourceSizeUncompressed;

        req.CancellationTag = 0;
        req.Name = (char*)path.c_str();

        queue->EnqueueRequest(&req);
        queue->Submit();

        return resource;
    }

    // Find best compressino for the given asset.
    int64_t CompressExhaustive(std::vector<uint8_t>& compressedDst, const std::vector<uint8_t>& uncompressedSrc, DSTORAGE_COMPRESSION_FORMAT* formatOut)
    {
        ZoneScoped;
        // @todo this much be updated as formats and levels are added.
        DSTORAGE_COMPRESSION_FORMAT supportedFormatMax = DSTORAGE_COMPRESSION_FORMAT_GDEFLATE;
        DSTORAGE_COMPRESSION_FORMAT supportedFormatMin = DSTORAGE_COMPRESSION_FORMAT_NONE;
        DSTORAGE_COMPRESSION supportedFormatLevelMax = DSTORAGE_COMPRESSION_BEST_RATIO;
        DSTORAGE_COMPRESSION supportedFormatLevelMin = DSTORAGE_COMPRESSION_FASTEST;

        int64_t smallestSize = INT64_MAX;

        std::vector<uint8_t> tempCompressedBuffer;

        for (std::underlying_type<DSTORAGE_COMPRESSION_FORMAT>::type format = supportedFormatMin; format <= supportedFormatMax; format++)
        {
            for (std::underlying_type<DSTORAGE_COMPRESSION>::type level = supportedFormatLevelMin; level <= supportedFormatLevelMax; level++)
            {
                int64_t compressedSize = Compress(static_cast<DSTORAGE_COMPRESSION_FORMAT>(format), static_cast<DSTORAGE_COMPRESSION>(level), tempCompressedBuffer, uncompressedSrc);
                if (compressedSize < smallestSize)
                {
                    smallestSize = compressedSize;
                    compressedDst = std::move(tempCompressedBuffer);
                    *formatOut = static_cast<DSTORAGE_COMPRESSION_FORMAT>(format);
                }
            }
        }
        return smallestSize;
    }

    int64_t Compress(DSTORAGE_COMPRESSION_FORMAT format, DSTORAGE_COMPRESSION compressionLevel, std::vector<uint8_t>& compressedDst, const std::vector<uint8_t>& uncompressedSrc)
    {
        ZoneScoped;
        if (format != DSTORAGE_COMPRESSION_FORMAT_NONE)
        {
            IDStorageCompressionCodec* codec;
            if (FAILED(DStorageCreateCompressionCodec(format, std::thread::hardware_concurrency(), IID_PPV_ARGS(&codec))))
            {
                std::wcerr << L"Unable to create compression codec. Check compression format or library version.";
                return -1;
            }

            auto compressedBytesMax = codec->CompressBufferBound(uncompressedSrc.size());
            compressedDst.resize(compressedBytesMax);

            // Note: For now we assume that compression is a benefit, but it might not be. It's best to check the actual compressed size and make a decision.
            size_t compressedBytesActual = 0;
            if (FAILED(codec->CompressBuffer(uncompressedSrc.data(), uncompressedSrc.size(), compressionLevel, compressedDst.data(), compressedDst.size(), &compressedBytesActual)))
            {
                std::wcerr << L"Compression failure.";
                return -1;
            }

            return compressedBytesActual;
        }
        else
        {
            if (compressedDst.size() < uncompressedSrc.size())
            {
                compressedDst.resize(uncompressedSrc.size());
            }
            memcpy(compressedDst.data(), uncompressedSrc.data(), uncompressedSrc.size());
            return uncompressedSrc.size();
        }
    }
    
    bool CreateFileOnDisk(const wchar_t* const path, HANDLE* handleInOut)
    {
        ZoneScoped;
        assert(handleInOut != nullptr);

        // Create files if necessary.
        if (*handleInOut == INVALID_HANDLE_VALUE)
        {
            *handleInOut = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                SetLastError(ERROR_SUCCESS); // ignore if already exist.
            }
            else if (GetLastError() != ERROR_SUCCESS)
            {
                std::wcerr << L"Failure to open file: " << path << std::endl;
                return false;
            }
        }

        return true;
    }

    int64_t WriteDataToDisk(const HANDLE fileHandle, const void* const data, const size_t byteCount)
    {
        ZoneScoped;
        LARGE_INTEGER filePointerOrStatus{ 0 };
        filePointerOrStatus.LowPart = SetFilePointer(fileHandle, 0, &filePointerOrStatus.HighPart, FILE_CURRENT);

        size_t totalBytesWritten = 0;
        const char* dataPtr = (char*)data;
        size_t bytesRemaining = byteCount;

        while (bytesRemaining > 0)
        {
            DWORD bytesToWrite = static_cast<DWORD>(std::min((uint)bytesRemaining, MAXDWORD));
            DWORD bytesWrittenThisCall = 0;
            if (WriteFile(fileHandle, dataPtr + totalBytesWritten, bytesToWrite, &bytesWrittenThisCall, NULL))
            {
                bytesRemaining -= bytesWrittenThisCall;
                dataPtr += bytesWrittenThisCall;
            }
            else
            {
                return -1;
            }
        }

        // Return prior offset in current file so we know where the data is located.
        return filePointerOrStatus.QuadPart;
    }

    assetID IsCached(String name)
    {
        ZoneScoped;
        String path;
        assetID id;
        id.hash = std::hash<std::string>{}(name);
        path = std::format("{}{}.tex", AssetLibrary::instance->assetsPath.c_str(), id.hash);

        std::ifstream fin(path, std::ios::binary);
        if (fin.is_open())
        {
            fin.close();
            return AssetLibrary::instance->Add(path, name, id);
            //return id;
        }
        return assetID::Invalid;
    }

    // mix of directXTex and
    //https://github.com/GPUOpen-LibrariesAndSDKs/DirectStorageSample/blob/main/src/TextureConverter/TextureConverter.cpp#L273
    assetID Write(String name)
    {
        ZoneScoped;
        bool compressionExhaustive = false;
        DSTORAGE_COMPRESSION_FORMAT compressionFormat = DSTORAGE_COMPRESSION_FORMAT_GDEFLATE;
        DSTORAGE_COMPRESSION compressionLevel = DSTORAGE_COMPRESSION_BEST_RATIO;

        String path;
        assetID id;
        id.hash = std::hash<std::string>{}(name);
        path = std::format("{}{}.dds", AssetLibrary::instance->assetsPath.c_str(), id.hash);

        String originalPath = AssetLibrary::instance->FindInImportPath(name);

        DirectX::TexMetadata metadata;
        DirectX::ScratchImage imageOriginal;
        bool needCompression = originalPath.find(".dds") == -1;

        HRESULT hr;
        {
            ZoneScopedN("LoadImage");
            hr = LoadImageFromDisk(originalPath, imageOriginal, &metadata) ? S_OK : E_FAIL;
        }
        if (FAILED(hr))
        {
            //IOs::Log("Fail to load {}", name.c_str());
            return assetID::Invalid;
        }

        if (!metadata.mipLevels || !metadata.arraySize)
            hr = E_INVALIDARG;

        if ((metadata.width > UINT32_MAX) || (metadata.height > UINT32_MAX)
            || (metadata.mipLevels > UINT16_MAX) || (metadata.arraySize > UINT16_MAX))
            hr = E_INVALIDARG;

        DirectX::ScratchImage* image = &imageOriginal;

        /*
        DirectX::ScratchImage converted;
        {
            ZoneScopedN("Convert");
            DirectX::Convert(image->GetImages(), image->GetImageCount(), metadata, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0, converted);
            metadata = converted.GetMetadata();
            image = &converted;
        }
        */

        DirectX::ScratchImage mipChain;
        {
            ZoneScopedN("Mips");
            DirectX::GenerateMipMaps(image->GetImages(), image->GetImageCount(), metadata, DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
            metadata = mipChain.GetMetadata();
            image = &mipChain;
        }

        DirectX::ScratchImage imageBC;
        if (needCompression)
        {
            DXGI_FORMAT compressFormat = metadata.GetAlphaMode() == DirectX::TEX_ALPHA_MODE_OPAQUE ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_BC7_UNORM;
            DirectX::TEX_COMPRESS_FLAGS compressFlag = compressFormat == DXGI_FORMAT_BC1_UNORM ? DirectX::TEX_COMPRESS_UNIFORM : DirectX::TEX_COMPRESS_BC7_QUICK;
            hr = DirectX::Compress(image->GetImages(), image->GetImageCount(), image->GetMetadata(), compressFormat, compressFlag | DirectX::TEX_COMPRESS_PARALLEL, DirectX::TEX_THRESHOLD_DEFAULT, imageBC);
            if (FAILED(hr))
            {
                IOs::Log("Fail to compress {}", name.c_str());
                return assetID::Invalid;
            }
            //DirectX::SaveToDDSFile(imageBC.GetImages(), imageBC.GetImageCount(), imageBC.GetMetadata(), DirectX::DDS_FLAGS_NONE, path.ToWString().c_str());
            metadata = imageBC.GetMetadata();
            image = &imageBC;
        }


        // Create resource desc.
        UINT subresourceCount = (uint)(std::max(metadata.arraySize, metadata.depth) * metadata.mipLevels);
        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = metadata.depth > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = (uint)metadata.width;
        resourceDesc.Height = (uint)metadata.height;
        resourceDesc.DepthOrArraySize = (unsigned short int)std::max(metadata.arraySize, metadata.depth);
        resourceDesc.MipLevels = (unsigned short int)metadata.mipLevels;
        resourceDesc.Format = metadata.format;
        resourceDesc.SampleDesc = { 1, 0 };
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> subresourceFootprints(subresourceCount);
        std::vector<UINT> subresourceRowsCount(subresourceCount);
        std::vector<UINT64> subresourceRowByteCount(subresourceCount);
        UINT64 subresourceTotalByteCount = 0;

        // Determine layout for disk.
        GPU::instance->device->GetCopyableFootprints(&resourceDesc
            , 0, subresourceCount
            , 0, &subresourceFootprints[0]
            , &subresourceRowsCount[0]
            , &subresourceRowByteCount[0]
            , &subresourceTotalByteCount);

        // Allocate memory to copy into.
        std::vector<uint8_t> textureData(subresourceTotalByteCount, 255);

        {
            ZoneScopedN("COPY");
            // copy texture data...
            for (UINT subResourceIdx = 0; subResourceIdx < subresourceCount; subResourceIdx++)
            {
                const auto& resourceFootprint = subresourceFootprints[subResourceIdx];
                const auto& footprint = resourceFootprint.Footprint;

                if (footprint.RowPitch > 256)
                {
                    const uint size = (uint)((subResourceIdx < subresourceCount - 1 ? subresourceFootprints[subResourceIdx + 1].Offset : subresourceTotalByteCount) - resourceFootprint.Offset);
                    auto resourcePtr = textureData.data() + resourceFootprint.Offset;
                    memcpy((char*)resourcePtr, image->GetImages()[subResourceIdx].pixels, size);
                }
                else
                {
                    // Src setup
                    size_t srcRowPitchBytes = ((DirectX::BitsPerPixel(metadata.format) * metadata.width) + 7) / 8; // rounded to nearest byte.
                    size_t srcSlicePitchBytes = srcRowPitchBytes * metadata.height; // assuming tightly packed image.

                    // Dst setup
                    auto resourcePtr = textureData.data() + resourceFootprint.Offset;
                    size_t dstRowPitchBytes = resourceFootprint.Footprint.RowPitch; // padded row pitch.
                    size_t dstRowPitchPackedBytes = subresourceRowByteCount[subResourceIdx];
                    size_t dstSlicePitchBytes = subresourceRowsCount[subResourceIdx] * dstRowPitchBytes;

                    size_t resolvedHeight = std::min(subresourceRowsCount[subResourceIdx], (uint)metadata.height);
                    size_t resolvedPackedRowPitch = std::min(std::min(dstRowPitchBytes, srcRowPitchBytes), dstRowPitchPackedBytes);

                    for (uint32_t y = 0; y < resolvedHeight; y++)
                    {
                        memcpy((char*)resourcePtr + y * dstRowPitchBytes, imageBC.GetImages()[subResourceIdx].pixels + y * resolvedPackedRowPitch, resolvedPackedRowPitch);
                    }
                }

            }
        }

        HANDLE metadataFileHandle = INVALID_HANDLE_VALUE;
        path = std::format("{}{}.meta", AssetLibrary::instance->assetsPath.c_str(), id.hash);
        if (!CreateFileOnDisk(path.ToWString().c_str(), &metadataFileHandle))
        {
            return assetID::Invalid;
        }

        HANDLE texturedataFileHandle = INVALID_HANDLE_VALUE;
        path = std::format("{}{}.tex", AssetLibrary::instance->assetsPath.c_str(), id.hash);
        if (!CreateFileOnDisk(path.ToWString().c_str(), &texturedataFileHandle))
        {
            return assetID::Invalid;
        }

        std::vector<uint8_t> gpuData;
        int64_t gpuDataSize = -1;
        if (compressionExhaustive)
        {
            gpuDataSize = CompressExhaustive(gpuData, textureData, &compressionFormat);
        }
        else if (compressionFormat != DSTORAGE_COMPRESSION_FORMAT_NONE)
        {
            // compression enabled.
            gpuData.resize(textureData.size());
            gpuDataSize = Compress(compressionFormat, compressionLevel, gpuData, textureData);
            if (gpuDataSize == -1)
            {
                IOs::Log("Failed to compress image: {}", path.c_str());
                return assetID::Invalid;
            }

            if ((uint)textureData.size() <= (uint)gpuDataSize)
            {
                // Turns out compression didn't help us at all. TODO: Determine threshold at which compression should be disabled.
                IOs::Log("Compression ineffective for {}", path.c_str());
            }
        }
        else
        {
            // no compression... avoiding allocation.
            gpuData = std::move(textureData);
            gpuDataSize = gpuData.size();
            if (gpuDataSize == 0)
            {
                return assetID::Invalid;
            }
        }

        // Write GPU Data and obtain offset to data.
        int64_t textureDataOffsetOnDisk = WriteDataToDisk(texturedataFileHandle, gpuData.data(), gpuDataSize);

        // Assemble metadata.
        DirectStorageSampleTextureMetadataHeader metadataDS;
        metadataDS.resourceDesc = resourceDesc;
        metadataDS.resourceSizeCompressed = gpuDataSize; // will be same as uncompressed size without compression.
        metadataDS.resourceSizeUncompressed = subresourceTotalByteCount;
        metadataDS.compressionFormat = compressionFormat;
        wcsncpy(metadataDS.resourceName, name.ToWString().c_str(), std::extent_v<decltype(metadataDS.resourceName)> -1);
        metadataDS.resourceName[std::extent_v<decltype(metadataDS.resourceName)> -1] = '\0'; // ensure truncation.
        metadataDS.resourceOffset = textureDataOffsetOnDisk;
        assert((textureDataOffsetOnDisk % 4096) == 0);

        // Write CPU Data.
        WriteDataToDisk(metadataFileHandle, &metadataDS, sizeof(metadataDS));

        // Align next write for Texture data.

        // now align the data..
        int64_t unalignedOffset = WriteDataToDisk(texturedataFileHandle, nullptr, 0);
        char zeroData[4096];
        int64_t dataAlignmentBytes = ((unalignedOffset + 4095) & (~4096 + 1)) - unalignedOffset;
        (void)WriteDataToDisk(texturedataFileHandle, zeroData, dataAlignmentBytes);

        CloseHandle(metadataFileHandle);
        CloseHandle(texturedataFileHandle);

        path = std::format("{}{}.tex", AssetLibrary::instance->assetsPath.c_str(), id.hash);
        return AssetLibrary::instance->Add(path, name, id);
    }

    // Generic "import this file (by name, resolved via FindInImportPath) as a texture asset"
    // entry point, factored out of MeshLoader::CreateOrLoadTexture's IsCached/Write/entity-creation
    // sequence so non-Assimp callers (e.g. the editor's texture picker) can reuse it directly.
    Components::Handle<Components::Texture> ImportByName(String texName)
    {
        ZoneScoped;
        assetID id = IsCached(texName);
        if (id == assetID::Invalid)
            id = Write(texName);
        if (id == assetID::Invalid)
        {
            IOs::Log("Fail to import texture {}", texName.c_str());
            return Components::Handle<Components::Texture>{ entityInvalid };
        }

        World::Entity ent;
        ent.Make(Components::Texture::mask | Components::Name::mask);
        strcpy_s(ent.Get<Components::Name>().name, 256, texName.c_str());
        ent.Get<Components::Texture>().id = id;
        return Components::Handle<Components::Texture>{ ent };
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
            fin.read((char*)&mesh.boundingSphere, sizeof(mesh.boundingSphere));
#define READ_VECTOR(vector)  { size_t size; fin.read((char*)&size, sizeof(size)); vector.resize(size); fin.read((char*)&vector[0], size * sizeof(vector[0])); }
            READ_VECTOR(mesh.vertices);
            uint lodCount = (uint)mesh.LODs.size();
            fin.read((char*)&lodCount, sizeof(uint));
            mesh.LODs.resize(lodCount);
            for (uint i = 0; i < mesh.LODs.size(); i++)
            {
                auto& lod = mesh.LODs[i];
                READ_VECTOR(lod.meshlets);
                READ_VECTOR(lod.meshlet_triangles);
                READ_VECTOR(lod.meshlet_vertices);
                READ_VECTOR(lod.indices);
            }
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
            fout.write((char*)&mesh.boundingSphere, sizeof(mesh.boundingSphere));
#define WRITE_VECTOR(vector)  { size_t size = vector.size(); fout.write((char*)&size, sizeof(size)); fout.write((char*)&vector[0], size * sizeof(vector[0])); }
            WRITE_VECTOR(mesh.vertices);
            uint lodCount = (uint)mesh.LODs.size();
            fout.write((char*)&lodCount, sizeof(uint));
            for (uint i = 0; i < mesh.LODs.size(); i++)
            {
                auto& lod = mesh.LODs[i];
                WRITE_VECTOR(lod.meshlets);
                WRITE_VECTOR(lod.meshlet_triangles);
                WRITE_VECTOR(lod.meshlet_vertices);
                WRITE_VECTOR(lod.indices);
            }
            fout.close();
        }

        String metaPath = std::format("{}{}.meta", AssetLibrary::instance->assetsPath.c_str(), id.hash);
        std::ofstream meta(metaPath, std::ios::binary);
        if (meta.is_open())
        {
            uint32_t type = (uint32_t)AssetLibrary::AssetType::mesh;
            uint32_t nameLen = (uint32_t)name.size();
            meta.write((char*)&type, sizeof(type));
            meta.write((char*)&nameLen, sizeof(nameLen));
            meta.write(name.c_str(), nameLen);
            meta.close();
        }

        return AssetLibrary::instance->Add(path, name, id);
    }

    // {hash}.omm sidecar next to the {hash}.mesh cache entry: baked opacity-micromap data,
    // written at import by the OMM bake and consumed by MeshStorage::LoadBLAS.
    static constexpr uint32_t ommMagic = 0x4D4D4F53; // 'SOMM'
    static constexpr uint32_t ommVersion = 1;

    String OMMPath(String meshPath)
    {
        std::string p = meshPath;
        return String(p.substr(0, p.find_last_of('.')) + ".omm");
    }

    bool ReadOMM(String meshPath, OMMData& omm)
    {
        ZoneScoped;
        std::ifstream fin((std::string)OMMPath(meshPath), std::ios::binary);
        if (!fin.is_open())
            return false;

        uint32_t magic = 0, version = 0, lodCount = 0;
        fin.read((char*)&magic, sizeof(magic));
        fin.read((char*)&version, sizeof(version));
        if (magic != ommMagic || version != ommVersion)
            return false;
        fin.read((char*)&omm.textureHash, sizeof(omm.textureHash));
        fin.read((char*)&omm.alphaThreshold, sizeof(omm.alphaThreshold));
        fin.read((char*)&lodCount, sizeof(lodCount));
        omm.LODs.resize(lodCount);
        for (auto& lod : omm.LODs)
        {
            fin.read((char*)&lod.lodIndex, sizeof(lod.lodIndex));
            fin.read((char*)&lod.indexFormat, sizeof(lod.indexFormat));
            READ_VECTOR(lod.indices);
            READ_VECTOR(lod.descs);
            READ_VECTOR(lod.histogram);
            READ_VECTOR(lod.arrayData);
        }
        return fin.good() && lodCount > 0;
    }

    void WriteOMM(String meshPath, OMMData& omm)
    {
        ZoneScoped;
        std::ofstream fout((std::string)OMMPath(meshPath), std::ios::binary);
        if (!fout.is_open())
            return;

        uint32_t lodCount = (uint32_t)omm.LODs.size();
        fout.write((char*)&ommMagic, sizeof(ommMagic));
        fout.write((char*)&ommVersion, sizeof(ommVersion));
        fout.write((char*)&omm.textureHash, sizeof(omm.textureHash));
        fout.write((char*)&omm.alphaThreshold, sizeof(omm.alphaThreshold));
        fout.write((char*)&lodCount, sizeof(lodCount));
        for (auto& lod : omm.LODs)
        {
            fout.write((char*)&lod.lodIndex, sizeof(lod.lodIndex));
            fout.write((char*)&lod.indexFormat, sizeof(lod.indexFormat));
            WRITE_VECTOR(lod.indices);
            WRITE_VECTOR(lod.descs);
            WRITE_VECTOR(lod.histogram);
            WRITE_VECTOR(lod.arrayData);
        }
    }

    // also DirectXMesh can do meshlets https://github.com/microsoft/DirectXMesh
    MeshData Process(MeshOriginal& originalMesh, uint LODLevelMin, uint LODLevelMax)
    {
		ZoneScoped;
        MeshData optimizedMesh;
        optimizedMesh.vertices = originalMesh.vertices;
        optimizedMesh.boundingSphere = originalMesh.boundingSphere;

        const float cone_weight = 0.5f;

        //meshopt_generateVertexRemap

        for (uint lodindex = LODLevelMin; lodindex <= LODLevelMax; lodindex++)
        {
            auto& lod = optimizedMesh.LODs.emplace_back();

            float threshold = lodindex == 0 ? 1.0f : (float)pow(0.25f, lodindex);
            size_t target_index_count = size_t(originalMesh.indices.size() * threshold);
            float target_error = 1e-2f;

            lod.indices.resize(originalMesh.indices.size());
            float lod_error = 0.f;
            uint newIndexCount = (uint)meshopt_simplify(&lod.indices[0], originalMesh.indices.data(), originalMesh.indices.size(), &originalMesh.vertices[0].px, originalMesh.vertices.size(), sizeof(Vertex), target_index_count, target_error, /* options= */ 0, &lod_error);
            lod.indices.resize(newIndexCount);

            size_t max_meshlets = meshopt_buildMeshletsBound(lod.indices.size(), HLSL::max_vertices, HLSL::max_triangles);
            std::vector<meshopt_Meshlet> meshopt_meshlets(max_meshlets);
            std::vector<unsigned int> meshlet_vertices(max_meshlets * HLSL::max_vertices);
            std::vector<unsigned char> meshlet_triangles(max_meshlets * HLSL::max_triangles * 3);

            size_t meshlet_count = meshopt_buildMeshlets(meshopt_meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), lod.indices.data(), lod.indices.size(), &originalMesh.vertices[0].px, originalMesh.vertices.size(), sizeof(Vertex), HLSL::max_vertices, HLSL::max_triangles, cone_weight);

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

            lod.meshlets = meshlets;
            lod.meshlet_vertices = meshlet_vertices;
            lod.meshlet_triangles = meshlet_triangles;
            lod.indices = lod.indices;
        
            for (uint i = 0; i < lod.meshlets.size(); i++)
            {
                seedAssert(lod.meshlets[i].vertexCount > 0);
                seedAssert(lod.meshlets[i].triangleCount > 0);
                seedAssert(lod.meshlets[i].vertexCount <= HLSL::max_vertices);
                seedAssert(lod.meshlets[i].triangleCount <= HLSL::max_triangles);
                seedAssert(lod.meshlets[i].vertexOffset < lod.meshlet_vertices.size());
                seedAssert(lod.meshlets[i].triangleOffset < lod.meshlet_triangles.size());
                //IOs::Log("V {} | T {}", lod.meshlets[i].vertexCount, lod.meshlets[i].triangleCount);
            }
        }

        return optimizedMesh;

        //use if (dot(normalize(cone_apex - camera_position), cone_axis) >= cone_cutoff) reject(); in mesh shader for cone culling
    }

    // Milestone 1 terrain (plan step 2): generates a flat XZ-plane grid (Y=0), spacing meters/vertex,
    // 'size' meters square, and registers it through the SAME upload path as imported meshes
    // (Process -> Write -> AssetLibrary) so it gets meshlet-ized identically (8-bit meshlet indices,
    // SNORM16 quantized positions -- no new mesh format code). Grid is centered on the origin so the
    // quadtree (World.h Systems::TerrainStreaming) can position/scale instances of it directly.
    // Called from the "Build Grid Mesh" editor button (Components::TerrainPropertyDraw, defined
    // below) and re-running it replaces the terrain's Handle<Mesh>.
    Components::Handle<Components::Mesh> BuildTerrainGridMesh(float spacing, float size)
    {
        ZoneScoped;

        if (spacing < 0.001f) spacing = 0.001f;
        if (size < spacing) size = spacing;

        uint count = (uint)roundf(size / spacing) + 1;
        if (count < 2) count = 2;

        // The requested spacing only sets the vertex COUNT; the actual per-vertex step must be
        // recomputed from size/(count-1) so the grid exactly spans [-size/2, size/2]. Using the
        // requested spacing directly here would fall short (or overshoot) whenever size isn't an
        // exact multiple of it, since (count-1)*spacing != size in that case -- the mesh would then
        // cover less (or more) than its authored "size", which every quadtree node's Transform scale
        // assumes it does exactly, producing gaps (or overlaps) between neighboring node patches.
        float actualSpacing = size / (float)(count - 1);

        MeshOriginal originalMesh;
        originalMesh.vertices.resize((size_t)count * (size_t)count);

        float half = size * 0.5f;
        for (uint z = 0; z < count; z++)
        {
            for (uint x = 0; x < count; x++)
            {
                Vertex& v = originalMesh.vertices[z * count + x];
                v = {};
                v.px = -half + x * actualSpacing;
                v.py = 0.0f;
                v.pz = -half + z * actualSpacing;
                v.nx = 0; v.ny = 1; v.nz = 0;
                v.tx = 1; v.ty = 0; v.tz = 0;
                v.bx = 0; v.by = 0; v.bz = 1;
                v.u = (float)x / (float)(count - 1);
                v.v = (float)z / (float)(count - 1);
                v.u1 = v.u; v.v1 = v.v;
            }
        }

        originalMesh.indices.resize((size_t)(count - 1) * (size_t)(count - 1) * 6);
        uint idx = 0;
        for (uint z = 0; z < count - 1; z++)
        {
            for (uint x = 0; x < count - 1; x++)
            {
                uint i0 = z * count + x;
                uint i1 = z * count + x + 1;
                uint i2 = (z + 1) * count + x;
                uint i3 = (z + 1) * count + x + 1;
                originalMesh.indices[idx++] = i0;
                originalMesh.indices[idx++] = i2;
                originalMesh.indices[idx++] = i1;
                originalMesh.indices[idx++] = i1;
                originalMesh.indices[idx++] = i2;
                originalMesh.indices[idx++] = i3;
            }
        }

        float3 minBB(-half, 0.0f, -half);
        float3 maxBB(half, 0.0f, half);
        float3 center = (minBB + maxBB) * 0.5f;
        float radius = length(minBB - maxBB) * 0.5f;
        originalMesh.boundingSphere = float4(center, radius);

        // Single LOD (0..0): the quadtree supplies LOD via node footprint / instance scale (coarser
        // nodes reuse this same mesh at a larger world-space scale -- the clipmap trick), so a
        // mesh-level LOD chain isn't needed for this milestone.
        MeshData meshData = Process(originalMesh, 0, 0);
        assetID id = Write(meshData, std::format("TerrainGrid_{}_{}", spacing, size));

        World::Entity ent;
        ent.Make(Components::Mesh::mask);
        ent.Get<Components::Mesh>().id = id;

        return Components::Handle<Components::Mesh>{ ent };
    }
};
MeshLoader* MeshLoader::instance = nullptr;

namespace Components
{
    // Declared in World.h; defined here so the "Build Grid Mesh" button can call MeshLoader
    // directly instead of going through a function pointer set up during MeshLoader::On().
    static void TerrainPropertyDraw(char* p)
    {
        DefaultPropertyDraw(Terrain::mask, p);

        Terrain* t = (Terrain*)p;
        if (MeshLoader::instance != nullptr && ImGui::Button("Build Grid Mesh"))
            t->gridMesh = MeshLoader::instance->BuildTerrainGridMesh(t->gridSpacing, t->gridSize);
    }
}

#include "../../Third/assimp-master/include/assimp/Importer.hpp"
#include "../../Third/assimp-master/include/assimp/Exporter.hpp"
#include "../../Third/assimp-master/include/assimp/scene.h"
#include "../../Third/assimp-master/include/assimp/postprocess.h"
#include "../../Third/assimp-master/include/assimp/GltfMaterial.h"
#include "../../Third/omm-main/include/omm.hpp"

// ---------------- OMM bake core ----------------
// Shared by the import-time bake (SceneLoader::BakeOMM, for sources that reference their albedo)
// and the runtime OMMBaker (for textures assigned in-engine, where the mesh<->texture pairing
// only exists in the world). Bakes opacity micromaps against the albedo alpha and writes the
// {hash}.omm sidecar consumed by MeshStorage::LoadBLAS.
namespace OMMBake
{
    // Bakes one LOD's index list; returns true only when the bake produced actual OMM data
    // (an all-opaque texture bakes to special indices only and is not worth a section).
    inline bool BakeLOD(omm::Baker baker, omm::Cpu::Texture texture, MeshData& mesh, uint lodIndex, OMMData::LOD& out)
    {
        ZoneScoped;
        auto& lod = mesh.LODs[lodIndex];

        omm::Cpu::BakeInputDesc input;
        input.bakeFlags = omm::Cpu::BakeFlags::EnableInternalThreads;
        input.texture = texture;
        // must match the runtime any-hit sampling: samplerLinear = s0 = linear, WRAP
        input.runtimeSamplerDesc = { omm::TextureAddressMode::Wrap, omm::TextureFilterMode::Linear, 0 };
        input.alphaMode = omm::AlphaMode::Test;
        input.texCoordFormat = omm::TexCoordFormat::UV32_FLOAT;
        input.texCoords = (const uint8_t*)mesh.vertices.data() + offsetof(Vertex, u);
        input.texCoordStrideInBytes = sizeof(Vertex);
        input.indexFormat = omm::IndexFormat::UINT_32;
        input.indexBuffer = lod.indices.data();
        input.indexCount = (uint32_t)lod.indices.size();
        input.alphaCutoff = 0.5f; // must match the any-hit alpha test
        input.format = omm::Format::OC1_4_State;
        input.maxSubdivisionLevel = 6;

        omm::Cpu::BakeResult result = 0;
        if (omm::Cpu::Bake(baker, input, &result) != omm::Result::SUCCESS)
            return false;

        const omm::Cpu::BakeResultDesc* res = nullptr;
        omm::Cpu::GetBakeResultDesc(result, &res);
        bool ok = res != nullptr && res->arrayDataSize > 0 && res->descArrayCount > 0 && res->indexCount > 0;
        if (ok)
        {
            out.lodIndex = lodIndex;
            out.indexFormat = res->indexFormat == omm::IndexFormat::UINT_16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            uint indexStride = res->indexFormat == omm::IndexFormat::UINT_16 ? 2 : 4;
            out.indices.assign((uint8_t*)res->indexBuffer, (uint8_t*)res->indexBuffer + (size_t)res->indexCount * indexStride);
            static_assert(sizeof(omm::Cpu::OpacityMicromapDesc) == sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC), "OMM SDK desc must alias the D3D12 desc");
            out.descs.assign((D3D12_RAYTRACING_OPACITY_MICROMAP_DESC*)res->descArray, (D3D12_RAYTRACING_OPACITY_MICROMAP_DESC*)res->descArray + res->descArrayCount);
            out.histogram.resize(res->descArrayHistogramCount);
            for (uint h = 0; h < res->descArrayHistogramCount; h++)
            {
                out.histogram[h].Count = res->descArrayHistogram[h].count;
                out.histogram[h].SubdivisionLevel = res->descArrayHistogram[h].subdivisionLevel;
                out.histogram[h].Format = (D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT)res->descArrayHistogram[h].format;
            }
            out.arrayData.assign((uint8_t*)res->arrayData, (uint8_t*)res->arrayData + res->arrayDataSize);
        }
        omm::Cpu::DestroyBakeResult(result);
        return ok;
    }

    // image is consumed. ALWAYS writes the sidecar: one with zero sections is the
    // "checked, nothing to gain" marker that stops the runtime baker from retrying every run.
    inline void BakeAndWrite(DirectX::ScratchImage&& image, uint64_t textureHash, MeshData& mesh, String meshPath)
    {
        ZoneScoped;
        OMMData ommData;
        ommData.textureHash = textureHash;
        ommData.alphaThreshold = 0.5f;

        if (DirectX::HasAlpha(image.GetMetadata().format))
        {
            // decompress/convert to RGBA8 and generate the full mip chain: the any-hit samples
            // mip 1, and feeding every mip keeps the bake conservative for any runtime mip
            DirectX::ScratchImage rgba;
            if (DirectX::IsCompressed(image.GetMetadata().format))
            {
                if (FAILED(DirectX::Decompress(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, rgba)))
                    return;
            }
            else if (image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM)
            {
                if (FAILED(DirectX::Convert(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba)))
                    return;
            }
            else
                rgba = std::move(image);

            DirectX::ScratchImage mipChain;
            if (rgba.GetMetadata().mipLevels > 1)
                mipChain = std::move(rgba);
            else if (FAILED(DirectX::GenerateMipMaps(*rgba.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, mipChain)))
                return;

            uint mipCount = (uint)mipChain.GetMetadata().mipLevels;
            std::vector<std::vector<uint8_t>> alphaMips(mipCount);
            std::vector<omm::Cpu::TextureMipDesc> mipDescs(mipCount);
            for (uint i = 0; i < mipCount; i++)
            {
                const DirectX::Image* mip = mipChain.GetImage(i, 0, 0);
                alphaMips[i].resize(mip->width * mip->height);
                for (uint y = 0; y < mip->height; y++)
                    for (uint x = 0; x < mip->width; x++)
                        alphaMips[i][y * mip->width + x] = mip->pixels[y * mip->rowPitch + x * 4 + 3];
                mipDescs[i].width = (uint32_t)mip->width;
                mipDescs[i].height = (uint32_t)mip->height;
                mipDescs[i].rowPitch = (uint32_t)mip->width;
                mipDescs[i].textureData = alphaMips[i].data();
            }

            omm::BakerCreationDesc bakerDesc;
            bakerDesc.type = omm::BakerType::CPU;
            omm::Baker baker = 0;
            if (omm::CreateBaker(bakerDesc, &baker) != omm::Result::SUCCESS)
                return;

            omm::Cpu::TextureDesc texDesc;
            texDesc.format = omm::Cpu::TextureFormat::UNORM8;
            texDesc.mips = mipDescs.data();
            texDesc.mipCount = mipCount;
            texDesc.alphaCutoff = 0.5f;
            omm::Cpu::Texture bakeTexture = 0;
            if (omm::Cpu::CreateTexture(baker, texDesc, &bakeTexture) == omm::Result::SUCCESS)
            {
                OMMData::LOD lod0;
                if (BakeLOD(baker, bakeTexture, mesh, 0, lod0))
                    ommData.LODs.push_back(std::move(lod0));
                if (mesh.LODs.size() > 1)
                {
                    OMMData::LOD lodLow;
                    if (BakeLOD(baker, bakeTexture, mesh, (uint)mesh.LODs.size() - 1, lodLow))
                        ommData.LODs.push_back(std::move(lodLow));
                }
                omm::Cpu::DestroyTexture(baker, bakeTexture);
            }
            omm::DestroyBaker(baker);
        }

        MeshLoader::instance->WriteOMM(meshPath, ommData);
        if (ommData.LODs.size() > 0)
            IOs::Log("baked OMM ({} lods) -> {}", ommData.LODs.size(), meshPath.c_str());
        else
            IOs::Log("baked OMM: nothing to gain (alpha is uniform), wrote empty marker -> {}", meshPath.c_str());
    }
}
#include <cstdarg>
class SceneLoader
{
    std::vector<World::Entity> meshIndexToEntity;
    std::vector<World::Entity> matIndexToEntity;
    std::vector<World::Entity> animationIndexToEntity;
    std::vector<World::Entity> skeletonIndexToEntity;
    std::vector<World::Entity> textureToEntity;

    uint meshCount = 2; // std mesh(containing lods) + rtmesh

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
        CreateEntities(_scene, _scene->mRootNode);
        CreateLights(_scene);
        CreateCameras(_scene);

        meshIndexToEntity.clear();
        matIndexToEntity.clear();
        animationIndexToEntity.clear();
        skeletonIndexToEntity.clear();
        textureToEntity.clear();

        editorState.dirtyHierarchy = true;
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
                parent.entity.Set(parentEntity);
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
                    parent.entity.Set(parentEntity);
                }

                auto& instance = ent.Get<Components::Instance>();
                instance.mesh = Components::Handle<Components::Mesh>{ meshIndexToEntity[node->mMeshes[i] * meshCount + 0] };
                instance.meshRT = Components::Handle<Components::Mesh>{ meshIndexToEntity[node->mMeshes[i] * meshCount + 1] };
                instance.material = Components::Handle<Components::Material>{ matIndexToEntity[_scene->mMeshes[node->mMeshes[i]]->mMaterialIndex] };
                instance.boundingSphereOverride = float4(0); // no override: use the mesh-derived sphere
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
                float3 maxBB = float3(-FLT_MAX);

                originalMesh.vertices.resize(m->mNumVertices);
                for (unsigned int j = 0; j < m->mNumVertices; j++)
                {
                    Vertex& v = originalMesh.vertices[j];

                    v.px = m->mVertices[j].x;
                    v.py = m->mVertices[j].y;
                    v.pz = m->mVertices[j].z;

                    minBB.x = min(minBB.x, v.px);
                    minBB.y = min(minBB.y, v.py);
                    minBB.z = min(minBB.z, v.pz);

                    maxBB.x = max(maxBB.x, v.px);
                    maxBB.y = max(maxBB.y, v.py);
                    maxBB.z = max(maxBB.z, v.pz);

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
                    if (m->HasTextureCoords(1))
                    {
                        v.u1 = m->mTextureCoords[1][j].x;
                        v.v1 = m->mTextureCoords[1][j].y;
                    }
                    if (m->HasTangentsAndBitangents())
                    {
                        v.tx = m->mTangents[j].x;
                        v.ty = m->mTangents[j].y;
                        v.tz = m->mTangents[j].z;
                        v.bx = m->mBitangents[j].x;
                        v.by = m->mBitangents[j].y;
                        v.bz = m->mBitangents[j].z;
                    }
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

                assetID id = assetID::Invalid;
                assetID idRT = assetID::Invalid;
                if (originalMesh.indices.size() != 0)
                {
                    MeshData mesh = MeshLoader::instance->Process(originalMesh, 0, 3);
                    id = MeshLoader::instance->Write(mesh, std::format("{}{}", m->mName.C_Str(), i));

                    MeshData meshRT = MeshLoader::instance->Process(originalMesh, 2, 2);
                    idRT = MeshLoader::instance->Write(mesh, std::format("{}{}_RTMesh", m->mName.C_Str(), i));

                    BakeOMM(_scene, m, mesh, id);
                }
                World::Entity ent;
                ent.Make(Components::Mesh::mask);
                ent.Get<Components::Mesh>().id = id;

                meshIndexToEntity.push_back(ent);


                World::Entity entRT;
                entRT.Make(Components::Mesh::mask);
                entRT.Get<Components::Mesh>().id = idRT;

                meshIndexToEntity.push_back(entRT);
            }
        }
    }

    // Resolves the material's albedo the same way CreateOrLoadTexture does (extension retries,
    // Unity .srgb case) and loads the source image. texNameOut is the final name whose
    // std::hash matches Components::Texture.id.hash for the DISABLE_OMMS mismatch check.
    bool LoadAlbedoImageForOMM(aiMaterial* m, String& texNameOut, DirectX::ScratchImage& imageOut)
    {
        ZoneScoped;
        aiString aitexName("");
        if (m->GetTextureCount(aiTextureType_BASE_COLOR))
            m->GetTexture(aiTextureType_BASE_COLOR, 0, &aitexName);
        else if (m->GetTextureCount(aiTextureType_DIFFUSE))
            m->GetTexture(aiTextureType_DIFFUSE, 0, &aitexName);

        String texName(aitexName.C_Str());
        if (texName.size() == 0)
            return false;

        int extensionTested = 0;
        String exts[] = { "jpg", "jpeg", "tif", "png" };
        while (true)
        {
            String originalPath = AssetLibrary::instance->FindInImportPath(texName);
            if (originalPath.size() > 0 && LoadImageFromDisk(originalPath, imageOut))
            {
                texNameOut = texName;
                return true;
            }
            if (extensionTested >= _countof(exts))
                return false;
            size_t unityStuff = texName.find(".srgb");
            if (unityStuff != std::string::npos)
                texName = texName.substr(0, unityStuff + 1);
            else
                texName = texName.substr(0, (uint)texName.find_last_of('.') + 1);
            texName += exts[extensionTested];
            extensionTested++;
        }
    }

    // Import-time OMM bake, for sources whose materials reference their albedo directly.
    // Sources with in-engine assigned textures (no texture in the file) are handled by the
    // runtime OMMBaker instead, which sees the live world's mesh<->material pairing.
    void BakeOMM(const aiScene* _scene, aiMesh* m, MeshData& mesh, assetID meshId)
    {
        ZoneScoped;
        aiMaterial* mat = _scene->mMaterials[m->mMaterialIndex];

        // cutout determination: the legacy opacity keys OR the glTF alphaMode. assimp maps
        // glTF alphaMode only to AI_MATKEY_GLTF_ALPHAMODE; AI_MATKEY_OPACITY is just
        // baseColorFactor.a, which stays 1 for MASK foliage.
        float opacity = 1.0f;
        mat->Get(AI_MATKEY_OPACITY, opacity);
        float transparency = 0.0f;
        mat->Get(AI_MATKEY_TRANSPARENCYFACTOR, transparency);
        aiString alphaMode("");
        mat->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode);
        bool alphaTested = strcmp(alphaMode.C_Str(), "MASK") == 0 || strcmp(alphaMode.C_Str(), "BLEND") == 0;
        if (opacity * (1.0f - transparency) >= 0.99f && !alphaTested)
            return;

        String texName;
        DirectX::ScratchImage image;
        if (!LoadAlbedoImageForOMM(mat, texName, image))
            return;

        String meshPath = std::format("{}{}.mesh", AssetLibrary::instance->assetsPath.c_str(), meshId.hash);
        OMMBake::BakeAndWrite(std::move(image), std::hash<std::string>{}(texName), mesh, meshPath);
    }

    Components::Handle<Components::Texture> CreateOrLoadTexture(aiMaterial* m, int channelCount, aiTextureType channels...)
    {
        ZoneScoped;
        va_list args;
        va_start(args, channels);

        int tests = 0;
        aiString aitexName("");
        while (aitexName.length == 0 && tests < channelCount)
        {
            aiTextureType t = static_cast<aiTextureType>(va_arg(args, int));
            if(m->GetTextureCount(t))
                m->GetTexture(t, 0, &aitexName);
            tests++;
        }

        String texName(aitexName.C_Str());
        assetID id = assetID::Invalid;

        World::Entity ent{ entityInvalid };
        if (texName.size() > 0)
        {
            int extensionTested = 0;
            String exts[] = { "jpg", "jpeg", "tif", "png"};
            while (id == assetID::Invalid)
            {
                // if the texture is found id should stay the same
                // if not it will be assetID::invalid
                id = TextureLoader::instance->IsCached(texName);
                if(id == assetID::Invalid)
                    id = TextureLoader::instance->Write(texName);

                if (id != assetID::Invalid)
                {
                    ent.Make(Components::Texture::mask | Components::Name::mask);

                    auto& name = ent.Get<Components::Name>();
                    strcpy_s(name.name, 256, texName.c_str());

                    ent.Get<Components::Texture>().id = id;
                    break;
                }
                else if (extensionTested < _countof(exts))
                {
                    size_t unityStuff = texName.find(".srgb");
                    if (unityStuff != std::string::npos) // special unity case
                    {
                        texName = texName.substr(0, unityStuff + 1);
                        texName += exts[extensionTested];
                        extensionTested++;
                    }
                    else
                    {
                        uint extStart = (uint)texName.find_last_of('.');
                        texName = texName.substr(0, extStart + 1);
                        texName += exts[extensionTested];
                        extensionTested++;
                    }
                }
                else
                {
                    break;
                }
            }
            if(id == assetID::Invalid)
                IOs::Log("Fail to load {}", texName.c_str());
        }
        else
        {
            id = assetID::Invalid;
        }
        
        textureToEntity.push_back(ent);

        return Components::Handle<Components::Texture> {ent};

        va_end(args);
    }

    void CreateMaterials(const aiScene* _scene)
    {
        ZoneScoped;

        // Registered locally (mirrors the single-entity registration this used to be); the shader
        // routing decision (which of these two a given material points at) now happens after the
        // cutout parameter is computed below, since the new generic multi-shader GBuffer draw
        // buckets by Material::shader rather than by parameters[4] at cull time.
        World::Entity shader;
        shader.Make(Components::Shader::mask);
        auto& shaderCmp = shader.Get<Components::Shader>();
        shaderCmp.id = AssetLibrary::instance->Add("src\\Shaders\\mesh.hlsl|DefaultG", "src\\Shaders\\mesh.hlsl|DefaultG");
        strcpy_s(shaderCmp.path, ECS_SHADER_PATH, "src\\Shaders\\mesh.hlsl|DefaultG");

        World::Entity shaderCutout;
        shaderCutout.Make(Components::Shader::mask);
        auto& shaderCutoutCmp = shaderCutout.Get<Components::Shader>();
        shaderCutoutCmp.id = AssetLibrary::instance->Add("src\\Shaders\\mesh.hlsl|DefaultGCutout", "src\\Shaders\\mesh.hlsl|DefaultGCutout");
        strcpy_s(shaderCutoutCmp.path, ECS_SHADER_PATH, "src\\Shaders\\mesh.hlsl|DefaultGCutout");

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

            for (uint j = 0; j < HLSL::MaterialTextureCount; j++)
            {
                newMat.textures[j] = { entityInvalid };
            }

            newMat.textures[0] = CreateOrLoadTexture(m, 2, aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE);
            newMat.textures[1] = CreateOrLoadTexture(m, 2, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS);
            newMat.textures[2] = CreateOrLoadTexture(m, 2, aiTextureType_METALNESS, aiTextureType_SPECULAR);
            newMat.textures[3] = CreateOrLoadTexture(m, 3, aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS, aiTextureType_HEIGHT);
            newMat.textures[4] = CreateOrLoadTexture(m, 2, aiTextureType_AMBIENT_OCCLUSION, aiTextureType_AMBIENT);
            newMat.textures[5] = CreateOrLoadTexture(m, 2, aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE);

            for (uint j = 0; j < HLSL::MaterialParametersCount; j++)
            {
                newMat.parameters[j] = 1.0f;
            }

            // Albedo scalar tint
            newMat.parameters[0] = 1.0f;
            aiColor4D baseColor(1, 1, 1, 1);
            if (m->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS || m->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS)
                newMat.parameters[0] = (baseColor.r + baseColor.g + baseColor.b) / 3.0f;

            // Roughness: prefer PBR factor, fall back to shininess conversion
            newMat.parameters[1] = 1.0f;
            float roughness = 1.0f;
            if (m->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
                newMat.parameters[1] = roughness;
            else
            {
                float shininess = 0.0f;
                if (m->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.0f)
                    newMat.parameters[1] = 1.0f - sqrtf(shininess / 1024.0f); // perceptual remap
            }

            // Metalness: PBR factor
            newMat.parameters[2] = 0.0f;
            float metalness = 0.0f;
            if (m->Get(AI_MATKEY_METALLIC_FACTOR, metalness) == AI_SUCCESS)
                newMat.parameters[2] = metalness;

            // Normal scale
            float normalScale = 1.0f;
            m->Get(AI_MATKEY_BUMPSCALING, normalScale);
            newMat.parameters[3] = normalScale;

            // Cutout threshold — parameters[4] = 0 means opaque (no alpha test).
            // AI_MATKEY_OPACITY: 1=fully opaque, 0=fully transparent.
            // AI_MATKEY_TRANSPARENCYFACTOR: 0=opaque, 1=transparent (inverse convention).
            newMat.parameters[4] = 0.0f;
            float opacity = 1.0f;
            m->Get(AI_MATKEY_OPACITY, opacity);
            float transparency = 0.0f;
            m->Get(AI_MATKEY_TRANSPARENCYFACTOR, transparency);
            float effectiveOpacity = opacity * (1.0f - transparency);
            if (effectiveOpacity < 0.99f)
                newMat.parameters[4] = effectiveOpacity;

            // Route to the cutout mesh shader so bucketing by Material::shader (the generic
            // multi-shader GBuffer draw) picks up the alpha-test PSO; parameters[4] keeps its
            // shading-threshold role in the cutout pixel shader regardless.
            newMat.shader = (newMat.parameters[4] != 0.0f) ? Components::Handle<Components::Shader>{ shaderCutout } : Components::Handle<Components::Shader>{ shader };

            matIndexToEntity.push_back(ent);
        }
    }

    void CreateAnimations(const aiScene* _scene)
    {
        ZoneScoped;
    }

    void CreateLights(const aiScene* _scene)
    {
        for (unsigned int i = 0; i < _scene->mNumLights; i++)
        {
            auto& l = _scene->mLights[i];

            World::Entity ent;
            ent.Find(l->mName.C_Str());

            if (!ent.IsValid())
                continue;

            Components::Light light;

            if (l->mAttenuationConstant <= 0 && l->mAttenuationLinear <= 0 && l->mAttenuationQuadratic <= 0)
                l->mAttenuationQuadratic = 1;

            if (l->mColorDiffuse.r == 0 && l->mColorDiffuse.g == 0 && l->mColorDiffuse.b == 0)
                light.color = float4(1, 1, 1, 1);
            else
                light.color = float4(l->mColorDiffuse.r, l->mColorDiffuse.g, l->mColorDiffuse.b, 1);
            float d = 1;
            float lightIntensity = 1.0f / (l->mAttenuationConstant + l->mAttenuationLinear * d + l->mAttenuationQuadratic * d * d);
            light.color.a = lightIntensity;
            light.range = lightIntensity * 20;

            light.castShadow = true;

            switch (l->mType)
            {
            case aiLightSourceType::aiLightSource_DIRECTIONAL :
                light.type = HLSL::LightType::Directional;
                break;
            case aiLightSourceType::aiLightSource_POINT:
                light.type = HLSL::LightType::Point;
                break;
            case aiLightSourceType::aiLightSource_SPOT:
                light.type = HLSL::LightType::Spot;
                break;
            default:
                light.type = HLSL::LightType::Point;
                break;
            }
            

            ent.Set(light);
        }
    }

    void CreateCameras(const aiScene* _scene)
    {

    }
};
SceneLoader* SceneLoader::instance = nullptr;

#include <deque>
#include <unordered_set>
#include <atomic>

// OMM baker behind the "bake OMM" button on the Instance component (for meshes whose alpha
// texture is assigned in-engine, where the import-time bake can't see the pairing, or to force
// a re-bake). Bakes on a worker thread and (re)writes the {hash}.omm sidecar, which
// MeshStorage::LoadBLAS picks up on the NEXT run (no live BLAS reload).
class OMMBaker
{
public:
    static OMMBaker* instance;

    void On()
    {
        instance = this;
        // populate AssetLibrary's lazy import-file map now, on the main thread: afterwards
        // FindInImportPath is read-only and safe to call from the worker
        AssetLibrary::instance->FindInImportPath("");
    }

    void Off()
    {
        stop = true;
        if (worker.joinable())
            worker.join();
        instance = nullptr;
    }

    // called from the editor UI (main thread): resolve everything that touches the world
    // here, so the worker never reads components
    void RequestBake(Components::Instance& instanceCmp)
    {
        if (!instanceCmp.mesh.IsValid() || !instanceCmp.material.IsValid())
            return;
        Components::Material& materialCmp = instanceCmp.material.Get();
        if (!materialCmp.textures[0].IsValid())
        {
            IOs::Log("bake OMM: the material has no albedo texture");
            return;
        }
        assetID meshId = instanceCmp.mesh.Get().id;
        if (!AssetLibrary::instance->map.contains(meshId))
        {
            IOs::Log("bake OMM: mesh is not in the asset library");
            return;
        }

        Request req;
        req.meshPath = AssetLibrary::instance->GetPath(meshId);
        req.texName = World::Entity(materialCmp.textures[0]).Get<Components::Name>().name;
        IOs::Log("bake OMM: queued {} <- {}", req.meshPath.c_str(), req.texName.c_str());

        std::lock_guard<std::mutex> guard(lock);
        queue.push_back(req);
        if (!worker.joinable())
            worker = std::thread([this]() { Work(); });
    }

private:
    struct Request
    {
        String meshPath; // {hash}.mesh cache path
        String texName;  // texture name: FindInImportPath key, and its hash is Components::Texture.id.hash
    };

    void Work()
    {
        while (!stop)
        {
            Request req;
            bool has = false;
            {
                std::lock_guard<std::mutex> guard(lock);
                if (!queue.empty())
                {
                    req = queue.front();
                    queue.pop_front();
                    has = true;
                }
            }
            if (!has)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            String srcPath = AssetLibrary::instance->FindInImportPath(req.texName);
            if (srcPath.size() == 0)
            {
                IOs::Log("bake OMM: source image not found for {}", req.texName.c_str());
                continue;
            }
            DirectX::ScratchImage image;
            if (!LoadImageFromDisk(srcPath, image))
            {
                IOs::Log("bake OMM: failed to load {}", srcPath.c_str());
                continue;
            }
            MeshData mesh = MeshLoader::instance->Read(req.meshPath);
            if (mesh.vertices.size() == 0 || mesh.LODs.size() == 0)
                continue;
            OMMBake::BakeAndWrite(std::move(image), std::hash<std::string>{}(req.texName), mesh, req.meshPath);
        }
    }

    std::thread worker;
    std::atomic<bool> stop = false;
    std::mutex lock;
    std::deque<Request> queue;
};
OMMBaker* OMMBaker::instance = nullptr;

namespace Components
{
    // Declared in World.h; defined here so the "bake OMM" button can call OMMBaker directly
    // instead of going through a function pointer set up during OMMBaker::On().
    static void InstancePropertyDraw(char* p)
    {
        DefaultPropertyDraw(Instance::mask, p);

        Instance* instance = (Instance*)p;
        ImGui::Spacing();
        if (OMMBaker::instance != nullptr && ImGui::Button("bake OMM"))
            OMMBaker::instance->RequestBake(*instance);
    }
}

#pragma comment(lib, "dxcompiler.lib")
#include "../../Third/DirectXShaderCompiler-main/inc/dxcapi.h"

// DXC reflection numbers for one compiled shader stage.
struct ReflectionStats
{
    uint instrTotal = 0;
    uint instrFloat = 0;
    uint instrInt = 0;
    uint instrTex = 0;
    uint tempRegs = 0;
    uint3 threadGroup = uint3(0, 0, 0);
};

// Collects per-shader-pass compile results so they can be dumped to shaderStats.csv for the
// live "edit -> hot-reload -> profile" loop. The handshake the external optimizer relies on:
//   - reloadId : bumped every time a pass is (re)compiled, so a fresh result is unambiguous.
//   - status   : OK / COMPILE_ERROR (+ error text, same string DXC prints to the console).
// A pass groups all its stages (mesh+pixel, etc.); GPU timings live in the #PASSES section,
// keyed by render-pass zone name (see Profiler), written by WriteCsv().
struct ShaderStatsCollector
{
    static ShaderStatsCollector* instance;

    struct Stage
    {
        String entry;
        String stage;          // ms_6_9 / ps_6_9 / cs_6_9 / vs_6_9 / lib_6_9
        ReflectionStats refl;
        uint dxilBytes = 0;
    };
    struct Pass
    {
        String file;
        String pass;
        uint reloadId = 0;
        bool compiledOk = true;
        String error;
        std::vector<Stage> stages;
    };

    std::mutex mtx;
    std::unordered_map<std::string, Pass> passes;   // key "file|pass"; element ptrs are stable across rehash
    static thread_local Pass* current;              // pass being compiled on the calling thread
    uint writeSeq = 0;

    static std::string Key(const String& file, const String& pass)
    {
        return std::string(file.c_str()) + "|" + std::string(pass.c_str());
    }

    void BeginPass(const String& file, const String& pass)
    {
        std::lock_guard<std::mutex> g(mtx);
        Pass& p = passes[Key(file, pass)];
        p.file = file;
        p.pass = pass;
        p.reloadId++;
        p.compiledOk = true;
        p.error.clear();
        p.stages.clear();
        current = &p;
    }

    void RecordStage(const String& entry, const String& stage, const ReflectionStats& refl, uint dxilBytes)
    {
        std::lock_guard<std::mutex> g(mtx);
        if (current == nullptr) return;
        Stage s;
        s.entry = entry;
        s.stage = stage;
        s.refl = refl;
        s.dxilBytes = dxilBytes;
        current->stages.push_back(s);
    }

    void RecordError(const String& error)
    {
        std::lock_guard<std::mutex> g(mtx);
        if (current == nullptr) return;
        current->compiledOk = false;
        if (current->error.empty())
            current->error = error;
    }

    void EndPass(bool compiled)
    {
        std::lock_guard<std::mutex> g(mtx);
        if (current != nullptr && !compiled)
            current->compiledOk = false;
        current = nullptr;
    }

    static std::string CsvEscape(const std::string& in)
    {
        std::string out = "\"";
        for (char c : in)
        {
            if (c == '\r') continue;
            if (c == '\n') { out += " | "; continue; }
            if (c == '"') { out += "\"\""; continue; }
            out += c;
        }
        out += "\"";
        return out;
    }

    // Atomically (tmp + rename) writes both sections. secondsSinceReload lets the optimizer know
    // the GPU timings have settled on the new shader before trusting them.
    void WriteCsv(const char* path, float secondsSinceReload)
    {
        std::lock_guard<std::mutex> g(mtx);
        uint seq = ++writeSeq;
        std::string tmp = std::string(path) + ".tmp";
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.is_open()) return;

        f << "writeSeq," << seq << ",secondsSinceReload," << secondsSinceReload << "\n";

        f << "#SHADERS\n";
        f << "file,pass,entry,stage,reloadId,status,instr_total,instr_float,instr_int,instr_tex,temp_regs,tg_x,tg_y,tg_z,dxil_bytes,error\n";
        for (auto& kv : passes)
        {
            Pass& p = kv.second;
            const char* status = p.compiledOk ? "OK" : "COMPILE_ERROR";
            std::string err = CsvEscape(std::string(p.error.c_str()));
            if (p.stages.empty())
            {
                f << p.file << "," << p.pass << ",,," << p.reloadId << "," << status
                  << ",,,,,,,,,," << err << "\n";
            }
            else
            {
                for (auto& s : p.stages)
                {
                    f << p.file << "," << p.pass << "," << s.entry << "," << s.stage << ","
                      << p.reloadId << "," << status << ","
                      << s.refl.instrTotal << "," << s.refl.instrFloat << "," << s.refl.instrInt << "," << s.refl.instrTex << ","
                      << s.refl.tempRegs << "," << s.refl.threadGroup.x << "," << s.refl.threadGroup.y << "," << s.refl.threadGroup.z << ","
                      << s.dxilBytes << "," << err << "\n";
                }
            }
        }

        f << "#PASSES\n";
        f << "zone,queue,gpu_ms_avg,gpu_ms_min,gpu_ms_max\n";
        if (Profiler::instance != nullptr)
        {
            const char* qn[3] = { "graphic", "compute", "copy" };
            for (uint q = 0; q < 3; q++)
            {
                for (auto& e : Profiler::instance->queueProfile[q].entries)
                {
                    if (e.name == nullptr || e.name[0] == '\0') continue;
                    double avg = 0, mn = 0, mx = 0;
                    int n = 0;
                    for (uint s = 0; s < Profiler::ProfileData::FilterSize; s++)
                    {
                        double t = e.TimeSamples[s];
                        if (t > 0.0)
                        {
                            if (n == 0) { mn = t; mx = t; }
                            else { mn = std::min(mn, t); mx = std::max(mx, t); }
                            avg += t;
                            n++;
                        }
                    }
                    if (n > 0) avg /= n;
                    f << e.name << "," << qn[q] << "," << avg << "," << mn << "," << mx << "\n";
                }
            }
        }

        f.close();
        MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING);
    }
};
ShaderStatsCollector* ShaderStatsCollector::instance = nullptr;
thread_local ShaderStatsCollector::Pass* ShaderStatsCollector::current = nullptr;

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
        static ShaderStatsCollector statsCollector;
        ShaderStatsCollector::instance = &statsCollector;
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

    D3D12_SHADER_BYTECODE Compile(String file, String entry, String type, Shader* shader, std::vector<String>* defines)
    {
		ZoneScoped;
        bool compiled = false;

        auto wfile = file.ToWString();
        auto includePath = std::wstring(L"src\\Shaders\\");
        auto entryName = std::wstring(entry.ToWString());
        auto typeName = std::wstring(type.ToWString());
        std::vector<std::wstring> wdefines;
        if (defines != nullptr)
        {
            for (auto& def : *defines)
            {
                wdefines.push_back(def.ToWString());
            }
        }

        //HRESULT hr;
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
        // Positional filename arg: without this, DXC has no real source name to embed in debug
        // info / PDB, so every compiled shader's errors (GBV, debug-layer, breadcrumbs) report a
        // generic placeholder filename instead of the actual .hlsl file -- makes every line number
        // in a GPU-based-validation message ambiguous about which file/include it's really in.
        vArgs.push_back(wfile.c_str());
        vArgs.push_back(L"-I");
        vArgs.push_back(includePath.c_str());
        vArgs.push_back(L"-E");
        vArgs.push_back(entryName.c_str());
        vArgs.push_back(L"-T");
        vArgs.push_back(typeName.c_str());
        vArgs.push_back(L"-rootsig-define");
        vArgs.push_back(L"SeeDRootSignature");
        vArgs.push_back(L"-enable-16bit-types");
        if (shader != nullptr && shader->type == Shader::Type::Raytracing)
        {
            vArgs.push_back(L"-enable-payload-qualifiers");
            vArgs.push_back(L"-D");
            vArgs.push_back(L"RAY_DISPATCH");
        }
        for (auto& def : wdefines)
        {
            vArgs.push_back(L"-D");
            vArgs.push_back(def.c_str());
        }
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
        //vArgs.push_back(L"-no-warnings");
#endif
        //vArgs.push_back(DXC_ARG_ALL_RESOURCES_BOUND);

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
            if (ShaderStatsCollector::instance) ShaderStatsCollector::instance->RecordError(errorMsg);
            return D3D12_SHADER_BYTECODE{};
        }
        // Quit if the compilation failed.
        HRESULT hrStatus;
        pResults->GetStatus(&hrStatus);
        if (!SUCCEEDED(hrStatus))
        {
            if (ShaderStatsCollector::instance) ShaderStatsCollector::instance->RecordError("compile failed (no error blob)");
            return D3D12_SHADER_BYTECODE{};
        }

        shaderBytecode = CreateShaderByteCode(shader, pResults);

        if (shader != nullptr)
        {
            CreateRootSignature(shader, pResults);
            CreateCommandSignature(shader);
            CreateRTShaderLibrary(shader, pResults);
        }

        ReflectionStats refl{};
        ShaderReflection(pResults, shader, &refl);
        if (ShaderStatsCollector::instance)
            ShaderStatsCollector::instance->RecordStage(entry, type, refl, (uint)shaderBytecode.BytecodeLength);

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
        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};

        if (shader->type == Shader::Type::Graphic)
            commandSignatureDesc.ByteStride = sizeof(HLSL::IndirectCommand);
        else if (shader->type == Shader::Type::Mesh)
            commandSignatureDesc.ByteStride = sizeof(HLSL::MeshletDrawCall);
        else if (shader->type == Shader::Type::Compute)
            commandSignatureDesc.ByteStride = sizeof(HLSL::InstanceCullingDispatch);
        else if (shader->type == Shader::Type::Raytracing)
            commandSignatureDesc.ByteStride = sizeof(HLSL::RayDispatch);

        if (shader->type == Shader::Type::Graphic)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2] = {};
            argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            argumentDescs[0].Constant.RootParameterIndex = InstanceIndexIndirectRegister;
            argumentDescs[0].Constant.Num32BitValuesToSet = 1;
            argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
            argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

            commandSignatureDesc.pArgumentDescs = argumentDescs;
            commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
            auto hr = GPU::instance->device->CreateCommandSignature(&commandSignatureDesc, shader->rootSignature, IID_PPV_ARGS(&shader->commandSignature));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
                seedAssert(false);
            }
        }
        else if (shader->type == Shader::Type::Mesh)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[3] = {};
            argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            argumentDescs[0].Constant.RootParameterIndex = InstanceIndexIndirectRegister;
            argumentDescs[0].Constant.Num32BitValuesToSet = 1;
            argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
            argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            argumentDescs[1].Constant.RootParameterIndex = meshletIndexIndirectRegister;
            argumentDescs[1].Constant.Num32BitValuesToSet = 1;
            argumentDescs[1].Constant.DestOffsetIn32BitValues = 0;
            argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

            commandSignatureDesc.pArgumentDescs = argumentDescs;
            commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
            auto hr = GPU::instance->device->CreateCommandSignature(&commandSignatureDesc, shader->rootSignature, IID_PPV_ARGS(&shader->commandSignature));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
                seedAssert(false);
            }
        }
        else if (shader->type == Shader::Type::Compute)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[3] = {};
            argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            argumentDescs[0].Constant.RootParameterIndex = InstanceIndexIndirectRegister;
            argumentDescs[0].Constant.Num32BitValuesToSet = 1;
            argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
            argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            argumentDescs[1].Constant.RootParameterIndex = meshletIndexIndirectRegister;
            argumentDescs[1].Constant.Num32BitValuesToSet = 1;
            argumentDescs[1].Constant.DestOffsetIn32BitValues = 0;
            argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

            commandSignatureDesc.pArgumentDescs = argumentDescs;
            commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
            auto hr = GPU::instance->device->CreateCommandSignature(&commandSignatureDesc, shader->rootSignature, IID_PPV_ARGS(&shader->commandSignature));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
                seedAssert(false);
            }
        }
        else if (shader->type == Shader::Type::Raytracing)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[3] = {};
            argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            argumentDescs[0].Constant.RootParameterIndex = InstanceIndexIndirectRegister;
            argumentDescs[0].Constant.Num32BitValuesToSet = 1;
            argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
            argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            argumentDescs[1].Constant.RootParameterIndex = meshletIndexIndirectRegister;
            argumentDescs[1].Constant.Num32BitValuesToSet = 1;
            argumentDescs[1].Constant.DestOffsetIn32BitValues = 0;
            argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS;

            commandSignatureDesc.pArgumentDescs = argumentDescs;
            commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
            auto hr = GPU::instance->device->CreateCommandSignature(&commandSignatureDesc, shader->rootSignature, IID_PPV_ARGS(&shader->commandSignature));
            if (FAILED(hr))
            {
                GPU::PrintDeviceRemovedReason(hr);
                seedAssert(false);
            }
        }


    }

    class RTPSOHelper
    {
    public:
        RTPSOHelper(Shader* _shader)
        {
            shader = _shader;
            payloadConfigMax = { 0, 0 };
        }

        void Reserve(uint numHitGroups, uint numMissShaders)
        {
            hitGroups.reserve(numHitGroups);
        }

        void AddPayloadConfig(uint maxPayloadSize, uint maxAttributeSize)
        {
            D3D12_RAYTRACING_SHADER_CONFIG payloadConfig = {};
            payloadConfig.MaxPayloadSizeInBytes = maxPayloadSize;
            payloadConfig.MaxAttributeSizeInBytes = maxAttributeSize;
            payloadConfigs.push_back(payloadConfig);
            payloadAssociation.resize(payloadConfigs.size());
            exportsPerPayloadConfig.resize(payloadConfigs.size());
        }

        void AddRaygen(std::wstring name)
        {
            shader->rayGen.push_back(name);

            allExportsStr.push_back(GetCachedStr(name));
            raygenExportsStr.push_back(GetCachedStr(name));
            raygenAndMissExportsStr.push_back(GetCachedStr(name));
        }

        void AddMiss(std::wstring name, uint payloadIndex)
        {
            shader->miss.push_back(name);

            allExportsStr.push_back(GetCachedStr(name));
            raygenAndMissExportsStr.push_back(GetCachedStr(name));
            exportsPerPayloadConfig[payloadIndex].push_back(GetCachedStr(name));
        }

        void AddHitGroup(std::wstring name, std::wstring closestHit, std::wstring anyHit, uint payloadIndex)
        {
            shader->hitGroups.push_back(name);

            //allExportsStr.push_back(GetCachedStr(name));
            allExportsStr.push_back(GetCachedStr(closestHit));
            allExportsStr.push_back(GetCachedStr(anyHit));
            
            exportsPerPayloadConfig[payloadIndex].push_back(GetCachedStr(closestHit));
            exportsPerPayloadConfig[payloadIndex].push_back(GetCachedStr(anyHit));
            hitGroups.push_back({ GetCachedStr(name), D3D12_HIT_GROUP_TYPE_TRIANGLES, GetCachedStr(anyHit), GetCachedStr(closestHit), nullptr /*intersection name*/ });
        }

        uint Build()
        {
            for (uint i = 0; i < allExportsStr.size(); i++)
            {
                allExportsDesc.push_back({ allExportsStr[i], nullptr, D3D12_EXPORT_FLAG_NONE});
            }

            for (uint i = 0; i < payloadConfigs.size(); i++)
            {
                payloadConfigMax.MaxPayloadSizeInBytes = max(payloadConfigMax.MaxPayloadSizeInBytes, payloadConfigs[i].MaxPayloadSizeInBytes);
                payloadConfigMax.MaxAttributeSizeInBytes = max(payloadConfigMax.MaxAttributeSizeInBytes, payloadConfigs[i].MaxAttributeSizeInBytes);
            }

            return
                1 +                                     // payloadconfig (only the max from all the configs)
                3 +                                     // Raygen and miss shaders : declaration + shader payload association
                (uint)hitGroups.size() * 2 +                  // Hit group : declarations + shader payload association
                1 +                                     // Empty global root signatures <- real root sig !!
                1 +                                     // Empty local root signatures <- can be use for some local param for each hit group. juste one empty local dummy for now
                1;                                      // Final pipeline subobject
        }

        Shader* shader;

        LPCWSTR GetCachedStr(std::wstring str)
        {
            for (uint i = 0; i < strCacheIndex; i++)
            {
                if (strCache[i] == str)
                    return strCache[i].c_str();
            }
            if (strCacheIndex < 128)
            {
                strCache[strCacheIndex] = str;
                return strCache[strCacheIndex++].c_str();
            }
            else
            {
                seedAssert(false); // too many unique strings, increase cache size or use a better system
                return L"";
            }
        }
        std::wstring strCache[128];
        uint strCacheIndex = 0;

        std::vector<D3D12_RAYTRACING_SHADER_CONFIG> payloadConfigs;
        D3D12_RAYTRACING_SHADER_CONFIG payloadConfigMax;
        std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> payloadAssociation;
        std::vector<LPCWSTR> allExportsStr;
        std::vector<D3D12_EXPORT_DESC> allExportsDesc;
        std::vector<LPCWSTR> raygenExportsStr;
        std::vector<LPCWSTR> raygenAndMissExportsStr;
        std::vector<std::vector<LPCWSTR>> exportsPerPayloadConfig;
        std::vector<D3D12_HIT_GROUP_DESC> hitGroups;
    };

    // https://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.5.pdf
    // https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways/
    // https://computergraphics.stackexchange.com/questions/10151/hit-group-export-already-defined
    void CreateRTShaderLibrary(Shader* shader, IDxcResult* pResults)
    {
        if (shader->type != Shader::Type::Raytracing)
            return;

        RTPSOHelper rtShader(shader);
        rtShader.Reserve(2, 2);  // Pre-reserve space for 2 hit groups and 2 miss shaders
        rtShader.AddPayloadConfig(sizeof(uint) * 6, sizeof(uint) * 2); // full payload for primary rays
        rtShader.AddPayloadConfig(sizeof(uint) * 3, sizeof(uint) * 2); // smaller payload for shadow rays
        rtShader.AddRaygen(L"RayGen");
        rtShader.AddHitGroup(L"HitGroup", L"ClosestHit", L"AnyHit", 0);
        rtShader.AddHitGroup(L"HitGroupShadow", L"ClosestHitShadow", L"AnyHitShadow", 1);
        rtShader.AddMiss(L"Miss", 0);
        rtShader.AddMiss(L"MissShadow", 1);
        UINT64 subobjectCount = rtShader.Build();

        // The pipeline is made of a set of sub-objects, representing the DXIL libraries, hit group
        // declarations, root signature associations, plus some configuration objects
        // Initialize a vector with the target object count. It is necessary to make the allocation before
        // adding subobjects as some subobjects reference other subobjects by pointer. Using push_back may
        // reallocate the array and invalidate those pointers.
        std::vector<D3D12_STATE_SUBOBJECT> subobjects(subobjectCount);
        UINT currentIndex = 0;

        // Add a subobject for the shader payload configuration
        D3D12_STATE_SUBOBJECT shaderConfigObject = {};
        shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderConfigObject.pDesc = &rtShader.payloadConfigMax;
        subobjects[currentIndex++] = shaderConfigObject;

        IDxcBlob* rtLibrary;
        pResults->GetResult(&rtLibrary);
        D3D12_DXIL_LIBRARY_DESC libDesc;
        libDesc.DXILLibrary.BytecodeLength = rtLibrary->GetBufferSize();
        libDesc.DXILLibrary.pShaderBytecode = rtLibrary->GetBufferPointer();
        libDesc.NumExports = (uint)rtShader.allExportsDesc.size();
        libDesc.pExports = rtShader.allExportsDesc.data();
        // Add all the DXIL libraries
        D3D12_STATE_SUBOBJECT libSubobject = {};
        libSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        libSubobject.pDesc = &libDesc;
        subobjects[currentIndex++] = libSubobject;

        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderPayloadAssociation = {};
        shaderPayloadAssociation.NumExports = (uint)rtShader.raygenExportsStr.size();
        shaderPayloadAssociation.pExports = rtShader.raygenExportsStr.data();
        shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[0];

        // Create and store the payload association object
        D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
        shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;
        subobjects[currentIndex++] = shaderPayloadAssociationObject;

        // Add all the hit group declarations
        for (uint i = 0; i < rtShader.hitGroups.size(); i++)
        {
            D3D12_STATE_SUBOBJECT hitGroup = {};
            hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            hitGroup.pDesc = &rtShader.hitGroups[i];
            subobjects[currentIndex++] = hitGroup;
        }

        for (uint i = 0; i < rtShader.exportsPerPayloadConfig.size(); i++)
        {
            D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION& shaderPayloadAssociation = rtShader.payloadAssociation[i];
            shaderPayloadAssociation.NumExports = (uint)rtShader.exportsPerPayloadConfig[i].size();
            shaderPayloadAssociation.pExports = rtShader.exportsPerPayloadConfig[i].data();
            shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[0];

            // Create and store the payload association object
            D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
            shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;
            subobjects[currentIndex++] = shaderPayloadAssociationObject;
        }

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
        // CONFIG1 so the pipeline can opt into opacity micromaps (required to trace a TLAS
        // referencing OMM-linked BLAS); plain CONFIG on pre-1.2 tiers
        D3D12_RAYTRACING_PIPELINE_CONFIG1 pipelineConfig = {};
        pipelineConfig.MaxTraceRecursionDepth = HLSL::maxRTDepth;
        pipelineConfig.Flags = GPU::instance->features.raytracingTier12 ? D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS : D3D12_RAYTRACING_PIPELINE_FLAG_NONE;

        D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
        pipelineConfigObject.Type = GPU::instance->features.raytracingTier12 ? D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1 : D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
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

        hr = shader->rtStateObject->QueryInterface(&shader->rtStateObjectProps);
        if (FAILED(hr))
        {
            //throw std::logic_error("Could not create the raytracing state object");
            std::cout << " !! Could not create the raytracing state object !! \n";
            assert(true);
        }
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

    void ShaderReflection(IDxcResult* pResults, Shader* shader = NULL, ReflectionStats* outStats = nullptr)
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

        {
            uint x = 0, y = 0, z = 0;
            pShaderReflection->GetThreadGroupSize(&x, &y, &z);
            if (outStats != nullptr)
            {
                outStats->instrTotal = refDesc.InstructionCount;
                outStats->instrFloat = refDesc.FloatInstructionCount;
                outStats->instrInt = refDesc.IntInstructionCount;
                outStats->instrTex = refDesc.TextureLoadInstructions;
                outStats->tempRegs = refDesc.TempRegisterCount;
                outStats->threadGroup = uint3(x, y, z);
            }
        }

        if (shader != NULL)
        {
            uint x;
            uint y;
            uint z;
            pShaderReflection->GetThreadGroupSize(&x, &y, &z);
            shader->numthreads = uint3(x, y, z);

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

                shader->outputs.push_back(format);
            }
        }
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

        }
        */
    }

    bool Parse(Shader& shader, String& file, String& shaderName)
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
                        compiled = Parse(shader, includeFile, shaderName);
					}
				}

                /*
                else if (line.find("SV_Target0") != -1)
                {
                    auto tokens = line.Split(" ");
                    for (uint i = 0; i < (uint)tokens.size(); i++)
                    {
                        if(tokens[i].find("DXGI") != -1)
                    }
                }
                */

				else if (line._Starts_with("#pragma "))
				{
					auto tokens = line.Split(" ");
                    // add empty strings for shaders passes names to avoid checking if a token is there
                    for (uint i = (uint)tokens.size(); i < 10; i++)
                    {
                        tokens.push_back("");
                    }

                    if (tokens[2] == shaderName) // only compile the requested shader
                    {
                        if (tokens[1] == "gBuffer")
                        {
                            auto defines = tokens[5].Split(",");

                            shader.type = Shader::Type::Mesh;
                            //D3D12_SHADER_BYTECODE amplificationShaderBytecode = Compile(file, tokens[2], "as_6_9", &shader);
                            D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[3], "ms_6_9", nullptr, &defines);
                            D3D12_SHADER_BYTECODE bufferShaderBytecode = Compile(file, tokens[4], "ps_6_9", &shader, &defines);
                            PipelineStateStream stream{};
                            D3D12_RT_FORMAT_ARRAY RTVFormats = {};
                            RTVFormats.NumRenderTargets = (uint)shader.outputs.size();
                            for (uint i = 0; i < (uint)shader.outputs.size(); i++)
                            {
                                RTVFormats.RTFormats[i] = shader.outputs[i];
                            }
                            for (uint i = (uint)shader.outputs.size(); i < 8; i++)
                            {
                                RTVFormats.RTFormats[i] = DXGI_FORMAT_UNKNOWN;
                            }
                            stream.RTFormats = RTVFormats;
                            //stream.AS = amplificationShaderBytecode;
                            stream.MS = meshShaderBytecode;
                            stream.PS = bufferShaderBytecode;
                            /*
                            // per shader depth desc ?
                            D3D12_DEPTH_STENCIL_DESC1& desc = stream.DepthStencil;
                            desc.DepthEnable = false;
                            desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
                            desc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
                            desc.StencilEnable = false;
                            desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
                            desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                            desc.BackFace = desc.FrontFace;
                            */

                            shader.pso = CreatePSO(stream);
                            compiled = shader.pso != nullptr;
                        }
                        else if (tokens[1] == "zPrepass")
                        {
                            auto defines = tokens[4].Split(",");

                            shader.type = Shader::Type::Mesh;
                            D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[3], "ms_6_9", &shader, &defines);
                            PipelineStateStream stream{};
                            stream.MS = meshShaderBytecode;
                            shader.pso = CreatePSO(stream);
                            compiled = shader.pso != nullptr;
                        }
                        else if (tokens[1] == "forward")
                        {
                            auto defines = tokens[5].Split(",");

                            shader.type = Shader::Type::Mesh;
                            //D3D12_SHADER_BYTECODE amplificationShaderBytecode = Compile(file, tokens[2], "as_6_9", &shader);
                            D3D12_SHADER_BYTECODE meshShaderBytecode = Compile(file, tokens[3], "ms_6_9", nullptr, &defines);
                            D3D12_SHADER_BYTECODE forwardShaderBytecode = Compile(file, tokens[4], "ps_6_9", &shader, &defines);
                            PipelineStateStream stream;
                            D3D12_RT_FORMAT_ARRAY RTVFormats = {};
                            RTVFormats.NumRenderTargets = (uint)shader.outputs.size();
                            for (uint i = 0; i < (uint)shader.outputs.size(); i++)
                            {
                                RTVFormats.RTFormats[i] = shader.outputs[i];
                            }
                            for (uint i = (uint)shader.outputs.size(); i < 8; i++)
                            {
                                RTVFormats.RTFormats[i] = DXGI_FORMAT_UNKNOWN;
                            }
                            stream.RTFormats = RTVFormats;
                            //stream.AS = amplificationShaderBytecode;
                            stream.MS = meshShaderBytecode;
                            stream.PS = forwardShaderBytecode;
                            shader.pso = CreatePSO(stream);
                            compiled = shader.pso != nullptr;
                        }
                        else if (tokens[1] == "debug")
                        {
                            auto defines = tokens[5].Split(",");

                            shader.type = Shader::Type::Graphic;
                            D3D12_SHADER_BYTECODE vertexShaderBytecode = Compile(file, tokens[3], "vs_6_9", nullptr, &defines);
                            D3D12_SHADER_BYTECODE pixelShaderBytecode = Compile(file, tokens[4], "ps_6_9", &shader, &defines);
                            PipelineStateStream stream{};
                            D3D12_RT_FORMAT_ARRAY RTVFormats = {};
                            RTVFormats.NumRenderTargets = (uint)shader.outputs.size();
                            for (uint i = 0; i < (uint)shader.outputs.size(); i++)
                            {
                                RTVFormats.RTFormats[i] = shader.outputs[i];
                            }
                            for (uint i = (uint)shader.outputs.size(); i < 8; i++)
                            {
                                RTVFormats.RTFormats[i] = DXGI_FORMAT_UNKNOWN;
                            }
                            stream.RTFormats = RTVFormats;
                            stream.DSVFormat = DXGI_FORMAT_UNKNOWN;
                            stream.VS = vertexShaderBytecode;
                            stream.PS = pixelShaderBytecode;
                            stream.PrimitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                            shader.pso = CreatePSO(stream);
                            shader.primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                            compiled = shader.pso != nullptr;
                        }
                        else if (tokens[1] == "compute")
                        {
                            auto defines = tokens[4].Split(",");

                            shader.type = Shader::Type::Compute;
                            D3D12_SHADER_BYTECODE computeShaderBytecode = Compile(file, tokens[3], "cs_6_9", &shader, &defines);
                            PipelineStateStream stream;
                            stream.CS = computeShaderBytecode;
                            stream.pRootSignature = shader.rootSignature;
                            shader.pso = nullptr;
                            shader.pso = CreatePSO(stream);
                            compiled = shader.pso != nullptr;
                        }
                        else if (tokens[1] == "raytracing")
                        {
                            auto defines = tokens[3].Split(",");

                            shader.type = Shader::Type::Raytracing;
                            D3D12_SHADER_BYTECODE computeShaderBytecode = Compile(file, "", "lib_6_9", &shader, &defines);
                            PipelineStateStream stream = {};
                            stream.pRootSignature = shader.rootSignature;
                            shader.pso = nullptr;
                            //shader.pso = CreatePSO(stream);
                            //compiled = shader.pso != nullptr;
                            compiled = computeShaderBytecode.BytecodeLength != 0;
                        }
                    }
				}
			}
		}
        return compiled;
    }

    bool Load(Shader& shader, String file, String shaderName)
    {
        if (ShaderStatsCollector::instance) ShaderStatsCollector::instance->BeginPass(file, shaderName);
        bool compiled = Parse(shader, file, shaderName);
        if (ShaderStatsCollector::instance) ShaderStatsCollector::instance->EndPass(compiled);
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
    CheckAssetsLifeTime();
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
            if (meshData.vertices.size() > 0)
            {
                OMMData ommData;
                bool hasOMM = MeshLoader::instance->ReadOMM(map[id].path, ommData);
                // GetOrCreate() inserts directly into MeshStorage::allMeshes and returns a
                // reference into it -- map-owned, not new'd, so we just point this asset's data at it.
                Mesh& mesh = MeshStorage::instance->GetOrCreate(meshData, commandBuffer.Get(), hasOMM ? &ommData : nullptr);
                lock.lock();
                map[id].data = &mesh;
                map[id].storageIndex = mesh.storageIndex;
                map[id].lastGetFrameCount = 0;
                assetsAlive.push_back(id);
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
            auto tokens = map[id].path.Split("|");

            Shader shader;
            bool compiled = ShaderLoader::instance->Load(shader, tokens[0], tokens[1]);
            if (compiled)
            {
                lock.lock();
                bool firstLoad = map[id].data == nullptr;
                uint index;
                Shader& slot = ShaderStorage::instance->GetOrCreate(firstLoad ? ~0u : map[id].storageIndex, index);
                if (firstLoad)
                {
                    map[id].data = &slot;
                    map[id].storageIndex = index;
                    assetsAlive.push_back(id);
                }
                *(Shader*)map[id].data = shader;
                map[id].lastGetFrameCount = 0;
                shaderLoaded++;
                lock.unlock();
            }
            else
            {
                // Failed compile: keep the last-good shader live, but copy the edited file's
                // timestamps onto it so NeedReload() doesn't recompile every frame (reload storm)
                // until the source actually changes again. Fixing the file bumps mtime -> reloads.
                if (map[id].data != nullptr)
                    ((Shader*)map[id].data)->creationTime = shader.creationTime;
                shader.Release();
            }
        }
    }
    break;
    case AssetLibrary::AssetType::texture:
    {
        if (ignoreBudget || textureLoaded < textureLoadingLimit)
        {
            Resource texture = TextureLoader::instance->Read(map[id].path, map[id].originalFilePath);
            if (texture.allocation != nullptr)
            {
                lock.lock();
                uint index;
                Resource& slot = TextureStorage::instance->GetOrCreate(texture, index);
                map[id].data = &slot;
                map[id].storageIndex = index;
                map[id].lastGetFrameCount = 0;
                assetsAlive.push_back(id);
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
