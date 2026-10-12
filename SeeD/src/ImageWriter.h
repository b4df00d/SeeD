#pragma once
// Writing an image out of the engine. The format follows the file extension, so
// callers just name the file they want.
//
//   .exr  32-bit float, linear, uncompressed -- written here by hand
//   .hdr  Radiance RGBE (8-bit mantissa, so lossy; fine for a look, not for numbers)
//   .dds  32-bit float
//   .png  8-bit, for eyeballing only
//
// EXR is the one that matters for calibration: it is the only lossless float
// format every viewer and the compare script can read. DirectXTex cannot write
// EXR without OpenEXR, which is not vendored, so the scanline writer below fills
// that gap. It emits NO_COMPRESSION on purpose -- that is what lets the Python
// side parse it with no decompressor (SeeD/tools/calibration/exrio.py).

#include <fstream>
#include <string>
#include <vector>

namespace ImageWriter
{
    namespace Detail
    {
        template <typename T>
        inline void Put(std::vector<char>& out, const T& value)
        {
            const char* p = reinterpret_cast<const char*>(&value);
            out.insert(out.end(), p, p + sizeof(T));
        }

        inline void PutBytes(std::vector<char>& out, const char* s, size_t n)
        {
            out.insert(out.end(), s, s + n);
        }

        inline void PutString(std::vector<char>& out, const char* s)
        {
            PutBytes(out, s, strlen(s) + 1); // null terminated
        }

        inline void PutAttribute(std::vector<char>& out, const char* name, const char* type,
                                 const void* data, int32_t size)
        {
            PutString(out, name);
            PutString(out, type);
            Put(out, size);
            PutBytes(out, reinterpret_cast<const char*>(data), (size_t)size);
        }

        inline String Extension(const String& path)
        {
            std::string s = (std::string)path;
            size_t dot = s.find_last_of('.');
            if (dot == std::string::npos) return String("");
            std::string ext = s.substr(dot + 1);
            for (auto& c : ext) c = (char)tolower((unsigned char)c);
            return String(ext.c_str());
        }
    }

    // Uncompressed scanline OpenEXR, three FLOAT channels. Channels are stored in
    // alphabetical order (B, G, R) within each scanline, which the spec requires.
    inline bool WriteExr(const String& path, uint width, uint height, const float* rgba)
    {
        using namespace Detail;
        if (width == 0 || height == 0 || rgba == nullptr) return false;

        std::vector<char> header;
        Put(header, (uint32_t)0x01312F76); // magic
        Put(header, (uint32_t)2);          // version 2, no flags

        // channels: name, pixelType(2=FLOAT), pLinear + 3 reserved, xSampling, ySampling
        std::vector<char> chlist;
        for (const char* c : { "B", "G", "R" })
        {
            PutString(chlist, c);
            Put(chlist, (int32_t)2);
            Put(chlist, (uint8_t)0);
            Put(chlist, (uint8_t)0);
            Put(chlist, (uint8_t)0);
            Put(chlist, (uint8_t)0);
            Put(chlist, (int32_t)1);
            Put(chlist, (int32_t)1);
        }
        chlist.push_back(0); // end of channel list
        PutAttribute(header, "channels", "chlist", chlist.data(), (int32_t)chlist.size());

        const uint8_t noCompression = 0;
        PutAttribute(header, "compression", "compression", &noCompression, 1);

        const int32_t box[4] = { 0, 0, (int32_t)width - 1, (int32_t)height - 1 };
        PutAttribute(header, "dataWindow", "box2i", box, sizeof(box));
        PutAttribute(header, "displayWindow", "box2i", box, sizeof(box));

        const uint8_t increasingY = 0;
        PutAttribute(header, "lineOrder", "lineOrder", &increasingY, 1);

        const float one = 1.0f;
        PutAttribute(header, "pixelAspectRatio", "float", &one, 4);
        const float center[2] = { 0.0f, 0.0f };
        PutAttribute(header, "screenWindowCenter", "v2f", center, sizeof(center));
        PutAttribute(header, "screenWindowWidth", "float", &one, 4);
        header.push_back(0); // end of header

        // Offsets have to be absolute file positions, so the table's own size counts.
        const uint64_t tableBytes = (uint64_t)height * sizeof(uint64_t);
        const uint64_t scanlineBytes = 2 * sizeof(int32_t) + (uint64_t)width * 3 * sizeof(float);
        uint64_t offset = (uint64_t)header.size() + tableBytes;

        std::vector<uint64_t> offsets(height);
        for (uint y = 0; y < height; y++)
        {
            offsets[y] = offset;
            offset += scanlineBytes;
        }

        std::ofstream f((std::string)path, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
        {
            IOs::Log("ImageWriter: cannot open {}", path.c_str());
            return false;
        }
        f.write(header.data(), (std::streamsize)header.size());
        f.write(reinterpret_cast<const char*>(offsets.data()), (std::streamsize)tableBytes);

        std::vector<float> row(width);
        for (uint y = 0; y < height; y++)
        {
            const int32_t lineY = (int32_t)y;
            const int32_t dataSize = (int32_t)(width * 3 * sizeof(float));
            f.write(reinterpret_cast<const char*>(&lineY), sizeof(lineY));
            f.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            for (uint c = 0; c < 3; c++)
            {
                const uint channel = (c == 0) ? 2u : (c == 1) ? 1u : 0u; // B, G, R
                const float* src = rgba + (size_t)y * width * 4;
                for (uint x = 0; x < width; x++) row[x] = src[(size_t)x * 4 + channel];
                f.write(reinterpret_cast<const char*>(row.data()), (std::streamsize)(width * sizeof(float)));
            }
        }
        f.close();
        return f.good();
    }

    // Anything DirectXTex already knows how to write. PNG goes through a UNORM
    // conversion first; it is a preview format, not a measurement.
    inline bool WriteViaDirectXTex(const String& path, uint width, uint height, const float* rgba,
                                   const String& ext)
    {
        DirectX::Image img = {};
        img.width = width;
        img.height = height;
        img.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        img.rowPitch = (size_t)width * 4 * sizeof(float);
        img.slicePitch = img.rowPitch * height;
        img.pixels = reinterpret_cast<uint8_t*>(const_cast<float*>(rgba));

        std::wstring wide;
        {
            std::string s = (std::string)path;
            wide.assign(s.begin(), s.end());
        }

        HRESULT hr = E_FAIL;
        if (ext == "hdr")
        {
            hr = DirectX::SaveToHDRFile(img, wide.c_str());
        }
        else if (ext == "dds")
        {
            hr = DirectX::SaveToDDSFile(img, DirectX::DDS_FLAGS_NONE, wide.c_str());
        }
        else if (ext == "png")
        {
            DirectX::ScratchImage converted;
            hr = DirectX::Convert(img, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                                  DirectX::TEX_THRESHOLD_DEFAULT, converted);
            if (SUCCEEDED(hr))
                hr = DirectX::SaveToWICFile(*converted.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
                                            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), wide.c_str());
        }

        if (FAILED(hr))
        {
            IOs::Log("ImageWriter: failed to write {} (hr 0x{:08x})", path.c_str(), (uint)hr);
            return false;
        }
        return true;
    }

    // rgba is tightly packed float RGBA, width*height*4, top row first.
    inline bool WriteImage(const String& path, uint width, uint height, const float* rgba)
    {
        String ext = Detail::Extension(path);
        if (ext == "exr") return WriteExr(path, width, height, rgba);
        if (ext == "hdr" || ext == "dds" || ext == "png")
            return WriteViaDirectXTex(path, width, height, rgba, ext);

        IOs::Log("ImageWriter: unsupported extension for {} (use .exr .hdr .dds or .png)", path.c_str());
        return false;
    }
}
