#ifndef __OPENPBR__
#define __OPENPBR__

// OpenPBR Surface 1.1 -- the single material model of the engine.
// Spec: https://academysoftwarefoundation.github.io/OpenPBR/
//
// This file owns everything material-side:
//   - OpenPBRSurface: the resolved parameters at one shading point, with the spec's names
//   - ResolveOpenPBR: HLSL::Material (structs.hlsl, GPU packing) + textures -> OpenPBRSurface
// The BSDF (closure prepare / eval / sample / pdf) lands here as well (roadmap step 2).
//
// Needs from common.hlsl, which includes this file: samplerLinear (binding.hlsl), SRGBToLinear, WorldNormal.

// emission_luminance is in nits (cd/m^2). SeeD stores Blender's radiometric units
// (SeeD/tools/calibration/README.md), converted with the same 683 lm/W the light importer uses
// (Loading.h CreateLights): radiance = nits / 683.
static const float OpenPBRNitsToRadiance = 1.0f / 683.0f;

struct OpenPBRSurface
{
    // base
    float  base_weight;
    float3 base_color;
    float  base_diffuse_roughness;
    float  base_metalness;
    // specular
    float  specular_weight;
    float3 specular_color;
    float  specular_roughness;
    float  specular_roughness_anisotropy;
    float  specular_ior;
    // transmission
    float  transmission_weight;
    float3 transmission_color;
    float  transmission_depth;
    float3 transmission_scatter;
    float  transmission_scatter_anisotropy;
    float  transmission_dispersion_scale;
    float  transmission_dispersion_abbe_number;
    // subsurface
    float  subsurface_weight;
    float3 subsurface_color;
    float  subsurface_radius;
    float3 subsurface_radius_scale;
    float  subsurface_scatter_anisotropy;
    // coat
    float  coat_weight;
    float3 coat_color;
    float  coat_roughness;
    float  coat_roughness_anisotropy;
    float  coat_ior;
    float  coat_darkening;
    // fuzz
    float  fuzz_weight;
    float3 fuzz_color;
    float  fuzz_roughness;
    // thin film
    float  thin_film_weight;
    float  thin_film_thickness; // micrometers
    float  thin_film_ior;
    // emission
    float3 emission;            // emission_luminance * emission_color, already converted to radiance
    // geometry
    float  geometry_opacity;
    bool   geometry_thin_walled;
    float3 normal;              // geometry_normal: shading normal, normal map applied
    float3 geometricNormal;     // interpolated vertex normal, no normal map
    float3 tangent;             // geometry_tangent
    float3 bitangent;
};

// Material texture fetch. Ray dispatches have no derivatives, so they read a fixed coarse mip;
// raster has them and biases one mip sharper.
float4 OpenPBRSampleTexture(uint textureIndex, float2 uv, float rayLod)
{
    Texture2D<float4> texture = ResourceDescriptorHeap[textureIndex];
#ifdef RAY_DISPATCH
    return texture.SampleLevel(samplerLinear, uv, rayLod);
#else
    return texture.SampleBias(samplerLinear, uv, -1);
#endif
}

// normal / tangent / bitangent: interpolated world-space vertex frame (tangent and bitangent may be
// 0 where the caller has none: the normal map is then skipped).
OpenPBRSurface ResolveOpenPBR(HLSL::Material m, float2 uv, float3 normal, float3 tangent, float3 bitangent)
{
    OpenPBRSurface s;

    s.base_weight                         = m.baseColorWeight.a;
    s.base_color                          = m.baseColorWeight.rgb;
    s.base_diffuse_roughness              = m.base.x;
    s.base_metalness                      = m.base.y;
    s.specular_weight                     = m.specularColorWeight.a;
    s.specular_color                      = m.specularColorWeight.rgb;
    s.specular_roughness                  = m.specular.x;
    s.specular_roughness_anisotropy       = m.specular.y;
    s.specular_ior                        = m.specular.z;
    s.transmission_weight                 = m.transmissionColorWeight.a;
    s.transmission_color                  = m.transmissionColorWeight.rgb;
    s.transmission_depth                  = m.transmission.x;
    s.transmission_scatter                = m.transmissionScatter.rgb;
    s.transmission_scatter_anisotropy     = m.transmissionScatter.a;
    s.transmission_dispersion_scale       = m.transmission.y;
    s.transmission_dispersion_abbe_number = m.transmission.z;
    s.subsurface_weight                   = m.subsurfaceColorWeight.a;
    s.subsurface_color                    = m.subsurfaceColorWeight.rgb;
    s.subsurface_radius                   = m.subsurfaceRadius.a;
    s.subsurface_radius_scale             = m.subsurfaceRadius.rgb;
    s.subsurface_scatter_anisotropy       = m.specular.w;
    s.coat_weight                         = m.coatColorWeight.a;
    s.coat_color                          = m.coatColorWeight.rgb;
    s.coat_roughness                      = m.coat.x;
    s.coat_roughness_anisotropy           = m.coat.y;
    s.coat_ior                            = m.coat.z;
    s.coat_darkening                      = m.coat.w;
    s.fuzz_weight                         = m.fuzzColorWeight.a;
    s.fuzz_color                          = m.fuzzColorWeight.rgb;
    s.fuzz_roughness                      = m.fuzzThinFilm.x;
    s.thin_film_weight                    = m.fuzzThinFilm.y;
    s.thin_film_thickness                 = m.fuzzThinFilm.z;
    s.thin_film_ior                       = m.fuzzThinFilm.w;
    s.geometry_opacity                    = m.base.z;
    s.geometry_thin_walled                = (m.flags & HLSL::MaterialFlagThinWalled) != 0;

    if (m.baseColorTexture != HLSL::invalidUINT)
    {
        float4 t = OpenPBRSampleTexture(m.baseColorTexture, uv, 1);
        s.base_color *= SRGBToLinear(t.rgb);
        s.geometry_opacity *= t.a; // alpha is linear coverage
    }
    if (m.specularRoughnessTexture != HLSL::invalidUINT)
        s.specular_roughness *= OpenPBRSampleTexture(m.specularRoughnessTexture, uv, 3).r;
    if (m.baseMetalnessTexture != HLSL::invalidUINT)
        s.base_metalness *= OpenPBRSampleTexture(m.baseMetalnessTexture, uv, 3).r;

    float3 emissionColor = m.emissionColorLuminance.rgb;
    if (m.emissionTexture != HLSL::invalidUINT)
        emissionColor *= SRGBToLinear(OpenPBRSampleTexture(m.emissionTexture, uv, 1).rgb);
    s.emission = emissionColor * (m.emissionColorLuminance.a * OpenPBRNitsToRadiance);

    s.geometricNormal = normalize(normal);
    s.normal = s.geometricNormal;
    s.tangent = tangent;
    s.bitangent = bitangent;
#ifndef RAY_DISPATCH // hit shaders build no tangent frame yet
    if (m.normalTexture != HLSL::invalidUINT)
    {
        float3 nrm = OpenPBRSampleTexture(m.normalTexture, uv, 3).xyz;
        float3x3 tbn = float3x3(normalize(tangent), normalize(bitangent), s.geometricNormal);
        s.normal = WorldNormal(nrm, tbn, m.base.w);
    }
#endif

    return s;
}

#endif // __OPENPBR__
