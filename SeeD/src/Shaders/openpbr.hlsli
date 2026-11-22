#ifndef __OPENPBR__
#define __OPENPBR__

// OpenPBR Surface 1.1 -- the single material model of the engine.
// Spec: https://academysoftwarefoundation.github.io/OpenPBR/
//
// This file owns everything material-side:
//   - OpenPBRSurface: the resolved parameters at one shading point, with the spec's names
//   - ResolveOpenPBR: HLSL::Material (structs.hlsl, GPU packing) + textures -> OpenPBRSurface
//   - OpenPBRClosure: the BSDF at that point for one view direction
//       OpenPBRPrepare -> OpenPBREval (next event estimation) / OpenPBRSample (path continuation)
//       OpenPBRGuides  -> DLSS-RR diffuse / specular albedo
//
// Model (spec "Formalism", lobe reduction with albedo scaling, energy evaluated toward the viewer):
//   fuzz over coat over mix(dielectric base, metal, base_metalness);
//   dielectric base = specular reflection + (1 - E_spec) * mix(diffuse, specular transmission, T)
// Lobes: fuzz (Zeltner sheen LTC fit), coat / metal / specular (anisotropic GGX, height-correlated
// Smith, Turquin multiple-scattering compensation), specular transmission (thin-walled sheet, or
// refraction into / out of a closed medium), diffuse (EON energy-preserving Oren-Nayar), plus the
// geometry_opacity pass-through.
// Not yet: subsurface transport (subsurface_color only tints the diffuse), thin film, dispersion,
// transmission volumes (transmission_color is a plain tint), coat/geometry normals, anisotropy on
// ray-traced hits (no tangents there).
//
// The fuzz and EON fits are ported from the OpenPBR reference viewer,
// https://github.com/jamportz/OpenPBR-viewer (MIT, Copyright (c) 2024 Jamie Portsmouth), which in
// turn follow MaterialX and "EON: A practical energy-preserving rough diffuse BRDF" (Portsmouth,
// Kutz, Hill 2024).
//
// Needs from common.hlsl, which includes this file: PI, samplerLinear (binding.hlsl), SRGBToLinear, WorldNormal.

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

// geometry_opacity alone, for alpha-test any-hit shaders (one texture fetch)
float OpenPBROpacity(HLSL::Material m, float2 uv)
{
    float opacity = m.base.z;
    if (m.baseColorTexture != HLSL::invalidUINT)
        opacity *= OpenPBRSampleTexture(m.baseColorTexture, uv, 1).a;
    return opacity;
}

// =============================================================================================
// Math
// =============================================================================================

static const float OpenPBRInvPi = 1.0f / PI;
static const float OpenPBRMinAlpha = 1.0e-4f;
static const float OpenPBREpsilon = 1.0e-7f;

float OpenPBRLuminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// Unpolarized dielectric Fresnel. eta = n_transmitted / n_incident, 1 on total internal reflection.
float OpenPBRFresnelDielectric(float cosI, float eta)
{
    float c = abs(cosI);
    float g2 = eta * eta - 1.0f + c * c;
    if (g2 <= 0.0f)
        return 1.0f;
    float g = sqrt(g2);
    float a = (g - c) / (g + c);
    float b = (c * (g + c) - 1.0f) / (c * (g - c) + 1.0f);
    return 0.5f * a * a * (1.0f + b * b);
}

float OpenPBRF0(float eta)
{
    float r = (eta - 1.0f) / (eta + 1.0f);
    return r * r;
}

float3 OpenPBRSchlick(float3 F0, float mu)
{
    float m = saturate(1.0f - mu);
    float m2 = m * m;
    return F0 + (1.0f - F0) * (m2 * m2 * m);
}

// Metal: F82-tint (spec "Metal"). specular_color sets the reflectance at mu = 1/7, where the real
// conductor curves dip; white reduces to plain Schlick.
float3 OpenPBRFresnelF82Tint(float mu, float3 F0, float3 tint)
{
    const float muBar = 1.0f / 7.0f;
    const float denominator = muBar * 0.39656311f; // muBar * (1 - muBar)^6
    float3 schlickBar = OpenPBRSchlick(F0, muBar);
    float m = saturate(1.0f - mu);
    float m3 = m * m * m;
    return saturate(OpenPBRSchlick(F0, mu) - (mu * m3 * m3 / denominator) * (schlickBar - tint * schlickBar));
}

// specular_weight folded into the IOR (spec "Specular"): scales F0 by the weight and keeps reflection
// and refraction consistent, where scaling the Fresnel term would break reciprocity.
float OpenPBRModulatedEta(float eta, float specularWeight)
{
    float epsilon = sign(eta - 1.0f) * sqrt(clamp(specularWeight * OpenPBRF0(eta), 0.0f, 0.99999f));
    return (1.0f + epsilon) / (1.0f - epsilon);
}

// --- GGX (all vectors in the local frame, z = shading normal) -------------------------------

// Spec "Specular": alpha = r^2, anisotropy stretches it along the tangent keeping the variance
float2 OpenPBRAlpha(float roughness, float anisotropy)
{
    float a = roughness * roughness;
    float b = 1.0f - anisotropy;
    float at = a * sqrt(2.0f / (1.0f + b * b));
    return max(float2(at, b * at), OpenPBRMinAlpha);
}

float OpenPBRGGXD(float3 m, float2 alpha)
{
    float3 s = float3(m.x / alpha.x, m.y / alpha.y, m.z);
    float d = dot(s, s);
    return 1.0f / (PI * alpha.x * alpha.y * d * d);
}

float OpenPBRGGXLambda(float3 w, float2 alpha)
{
    float z2 = max(w.z * w.z, OpenPBREpsilon);
    float t = (alpha.x * alpha.x * w.x * w.x + alpha.y * alpha.y * w.y * w.y) / z2;
    return 0.5f * (sqrt(1.0f + t) - 1.0f);
}

float OpenPBRGGXG1(float3 w, float2 alpha)
{
    return 1.0f / (1.0f + OpenPBRGGXLambda(w, alpha));
}

// height-correlated masking-shadowing
float OpenPBRGGXG2(float3 wi, float3 wo, float2 alpha)
{
    return 1.0f / (1.0f + OpenPBRGGXLambda(wi, alpha) + OpenPBRGGXLambda(wo, alpha));
}

// Visible normal sampling, spherical caps (Dupuy & Benyoub 2023). wi.z > 0.
// pdf(m) = G1(wi) * max(0, wi.m) * D(m) / wi.z
float3 OpenPBRSampleGGXVNDF(float3 wi, float2 alpha, float2 u)
{
    float3 wiStd = normalize(float3(wi.xy * alpha, wi.z));
    float phi = 2.0f * PI * u.x;
    float z = (1.0f - u.y) * (1.0f + wiStd.z) - wiStd.z;
    float sinTheta = sqrt(saturate(1.0f - z * z));
    float3 mStd = float3(sinTheta * cos(phi), sinTheta * sin(phi), z) + wiStd;
    return normalize(float3(mStd.xy * alpha, mStd.z));
}

// Directional albedo of a GGX reflection lobe with Schlick Fresnel F0 (split-sum DFG fit from the
// DLSS-RR integration guide appendix / Ray Tracing Gems ch. 32). alpha = roughness^2.
float3 OpenPBRGGXAlbedo(float3 F0, float alpha, float NoV)
{
    NoV = abs(NoV);
    float4 X = float4(1.0f, NoV, NoV * NoV, NoV * NoV * NoV);
    float4 Y = float4(1.0f, alpha, alpha * alpha, alpha * alpha * alpha);
    float2x2 M1 = float2x2(0.99044f, -1.28514f, 1.29678f, -0.755907f);
    float3x3 M2 = float3x3(1.f, 2.92338f, 59.4188f, 20.3225f, -27.0302f, 222.592f, 121.563f, 626.13f, 316.627f);
    float2x2 M3 = float2x2(0.0365463f, 3.32707f, 9.0632f, -9.04756f);
    float3x3 M4 = float3x3(1.f, 3.59685f, -1.36772f, 9.04401f, -16.3174f, 9.22949f, 5.56589f, 19.7886f, -20.2123f);
    float bias = dot(mul(M1, X.xy), Y.xy) * rcp(dot(mul(M2, X.xyw), Y.xyw));
    float scale = dot(mul(M3, X.xy), Y.xy) * rcp(dot(mul(M4, X.xzw), Y.xyw));
    bias *= saturate(F0.g * 50.0f); // no reflectance, no bias
    return saturate(mad(F0, max(0.0f, scale), max(0.0f, bias)));
}

// Multiple-scattering compensation (Turquin 2019): the single-scattering lobe loses 1 - E_ss of the
// energy, given back in proportion to F0.
float3 OpenPBRMultiScatter(float3 F0, float alpha, float NoV)
{
    float Ess = OpenPBRGGXAlbedo(1.0f, alpha, NoV).x;
    return 1.0f + F0 * ((1.0f - Ess) / max(Ess, 1.0e-3f));
}

// --- Fuzz: Zeltner et al. 2022 sheen, fits from the OpenPBR viewer / MaterialX ----------------

float OpenPBRFuzzAlbedo(float x, float y) // x = cos(view), y = fuzz_roughness
{
    float s = y * (0.0206607f + 1.58491f * y) / (0.0379424f + y * (1.32227f + y));
    float m = y * (-0.193854f + y * (-1.14885f + y * (1.7932f - 0.95943f * y * y))) / (0.046391f + y);
    float o = y * (0.000654023f + (-0.0207818f + 0.119681f * y) * y) / (1.26264f + y * (-1.92021f + y));
    float d = (x - m) / s;
    return exp(-0.5f * d * d) / (s * sqrt(2.0f * PI)) + o;
}
float OpenPBRFuzzLTCaInv(float x, float y)
{
    return (2.58126f * x + 0.813703f * y) * y / (1.0f + 0.310327f * x * x + 2.60994f * x * y);
}
float OpenPBRFuzzLTCbInv(float x, float y)
{
    return sqrt(1.0f - x) * (y - 1.0f) * y * y * y / (0.0000254053f + 1.71228f * x - 1.71506f * x * y + 1.34174f * y * y);
}
// LTC frame: x along the view's projection on the tangent plane
float3x3 OpenPBRFuzzBasis(float3 wi)
{
    float lenSqr = dot(wi.xy, wi.xy);
    float2 x = lenSqr > 0.0f ? wi.xy * rsqrt(lenSqr) : float2(1.0f, 0.0f);
    return float3x3(float3(x, 0.0f), float3(-x.y, x.x, 0.0f), float3(0.0f, 0.0f, 1.0f)); // rows
}

// --- Diffuse: EON (Portsmouth, Kutz, Hill 2024), as in the OpenPBR viewer --------------------

static const float OpenPBRConstant1FON = 0.5f - 2.0f / (3.0f * PI);
static const float OpenPBRConstant2FON = 2.0f / 3.0f - 28.0f / (15.0f * PI);

float OpenPBRFONAlbedo(float mu, float r) // E_FON_approx
{
    float mucomp = 1.0f - mu;
    float mucomp2 = mucomp * mucomp;
    float GoverPi = mucomp * (0.0571085289f + 0.491881867f * mucomp) + mucomp2 * mucomp * (-0.332181442f + 0.0714429953f * mucomp);
    return (1.0f + r * GoverPi) / (1.0f + OpenPBRConstant1FON * r);
}

float3 OpenPBREONMultiScatterAlbedo(float3 rho, float avgEF)
{
    return (rho * rho) * avgEF / (1.0f - rho * (1.0f - avgEF));
}

// f_EON: wi, wo directed away from the surface, local frame
float3 OpenPBREON(float3 rho, float r, float3 wi, float3 wo)
{
    float s = dot(wi, wo) - wi.z * wo.z;
    float sovertF = s > 0.0f ? s / max(wi.z, wo.z) : s;
    float AF = 1.0f / (1.0f + OpenPBRConstant1FON * r);
    float3 fss = (rho * OpenPBRInvPi) * AF * (1.0f + r * sovertF);
    float EFo = OpenPBRFONAlbedo(wo.z, r);
    float EFi = OpenPBRFONAlbedo(wi.z, r);
    float avgEF = AF * (1.0f + OpenPBRConstant2FON * r);
    float3 rhoMs = OpenPBREONMultiScatterAlbedo(rho, avgEF);
    float3 fms = (rhoMs * OpenPBRInvPi) * max(OpenPBREpsilon, 1.0f - EFo) * max(OpenPBREpsilon, 1.0f - EFi) / max(OpenPBREpsilon, 1.0f - avgEF);
    return fss + fms;
}

float3 OpenPBREONAlbedo(float3 rho, float r, float mu)
{
    float AF = 1.0f / (1.0f + OpenPBRConstant1FON * r);
    float EF = OpenPBRFONAlbedo(mu, r);
    float avgEF = AF * (1.0f + OpenPBRConstant2FON * r);
    return rho * EF + OpenPBREONMultiScatterAlbedo(rho, avgEF) * (1.0f - EF);
}

float3 OpenPBRSampleCosine(float2 u)
{
    float r = sqrt(u.x);
    float phi = 2.0f * PI * u.y;
    return float3(r * cos(phi), r * sin(phi), sqrt(saturate(1.0f - u.x)));
}

// =============================================================================================
// Closure
// =============================================================================================

// OpenPBRSample flags
static const uint OpenPBRSampleGlossy = 1u << 0;        // a microfacet lobe (not diffuse / fuzz)
static const uint OpenPBRSampleTransmission = 1u << 1;  // the direction crossed the surface
static const uint OpenPBRSampleCrossesMedium = 1u << 2; // ... into or out of a closed medium (not thin-walled / pass-through)

struct OpenPBRClosure
{
    // frame: N is the shading normal and Ng the geometric one, both turned toward the viewer
    float3 N;
    float3 T;
    float3 B;
    float3 Ng;
    float3 V;
    float3 Vl;              // V in the local frame
    bool inside;            // V inside a transmissive closed medium: only the dielectric interface

    // lobe parameters
    float3 fuzzColor;       // fuzz_color * directional albedo
    float fuzzRoughness;
    float2 coatAlpha;
    float coatEta;
    float3 coatMultiScatter;
    float2 specAlpha;       // shared by metal, specular reflection and transmission
    float specRoughness;    // r, after coat roughening
    float specEta;          // modulated, outside -> inside (inverted when inside)
    float3 specularColor;
    float3 specMultiScatter;
    float3 metalF0;
    float3 metalTint;
    float3 metalMultiScatter;
    float3 diffuseColor;    // rho
    float diffuseRoughness;
    bool thinWalled;

    // lobe weights (multiply each lobe's BSDF) and selection probabilities
    float3 wFuzz;
    float3 wCoat;
    float3 wMetal;
    float3 wSpec;
    float3 wTrans;
    float3 wDiff;
    float wTransparent;
    float pFuzz;
    float pCoat;
    float pMetal;
    float pSpec;
    float pTrans;
    float pDiff;
    float pTransparent;

    // directional albedos toward V (lobe weight included) for the guides
    float3 eSpecular;
    float3 eDiffuse;

    float3 emission;        // leaving toward V, through coat and fuzz
};

float3 OpenPBRToLocal(OpenPBRClosure c, float3 w)
{
    return float3(dot(w, c.T), dot(w, c.B), dot(w, c.N));
}

float3 OpenPBRToWorld(OpenPBRClosure c, float3 w)
{
    return w.x * c.T + w.y * c.B + w.z * c.N;
}

// V: unit vector from the shading point toward the viewer.
OpenPBRClosure OpenPBRPrepare(OpenPBRSurface s, float3 V)
{
    OpenPBRClosure c = (OpenPBRClosure)0;
    c.V = V;

    // ---- sidedness ---------------------------------------------------------------------------
    // Back side of a closed transmissive object: the ray is inside the medium, only the
    // dielectric interface remains. Every other back side (thin walls, single planes, double-sided
    // cards) shades like the front.
    float3 Ng = s.geometricNormal;
    bool frontFace = dot(Ng, V) >= 0.0f;
    float transmission = s.transmission_weight * (1.0f - s.base_metalness);
    c.thinWalled = s.geometry_thin_walled;
    c.inside = !frontFace && !c.thinWalled && transmission > 0.0f;
    float3 N = s.normal;
    if (!frontFace)
    {
        Ng = -Ng;
        N = -N;
    }
    // normal maps can tilt N away from the viewer: bend it back just enough
    float NdotV = dot(N, V);
    if (NdotV < 1.0e-3f)
        N = normalize(N + V * (1.0e-3f - NdotV));
    c.N = N;
    c.Ng = Ng;

    float3 tangent = s.tangent - N * dot(N, s.tangent);
    if (dot(tangent, tangent) > 1.0e-6f)
    {
        c.T = normalize(tangent);
        c.B = cross(N, c.T);
    }
    else // no tangent (ray-traced hits): any frame, anisotropy is then meaningless anyway
    {
        float sgn = N.z >= 0.0f ? 1.0f : -1.0f;
        float a = -1.0f / (sgn + N.z);
        float b = N.x * N.y * a;
        c.T = float3(1.0f + sgn * N.x * N.x * a, sgn * b, -sgn * N.x);
        c.B = float3(b, sgn + N.y * N.y * a, -N.y);
    }
    c.Vl = OpenPBRToLocal(c, V);
    float mu = saturate(c.Vl.z);

    // ---- specular interface (shared by metal, reflection and transmission) -------------------
    float C = s.coat_weight;
    // the base sees the coat's IOR above it where coated; a coat denser than the base would
    // trap light by total internal reflection, so that ratio is flipped (spec "Coat")
    float etaBaseCoat = s.specular_ior / max(s.coat_ior, 1.0e-3f);
    if (etaBaseCoat < 1.0f)
        etaBaseCoat = 1.0f / etaBaseCoat;
    float etaSpec = lerp(s.specular_ior, etaBaseCoat, C);
    c.specEta = OpenPBRModulatedEta(max(etaSpec, 1.0e-3f), s.specular_weight);
    // the coat's roughness blurs what lies under it
    float rB = s.specular_roughness;
    float rC = s.coat_roughness;
    c.specRoughness = lerp(rB, sqrt(sqrt(min(1.0f, rB * rB * rB * rB + 2.0f * rC * rC * rC * rC))), C);
    c.specAlpha = OpenPBRAlpha(c.specRoughness, s.specular_roughness_anisotropy);
    float specAlphaIso = sqrt(c.specAlpha.x * c.specAlpha.y);

    if (c.inside)
    {
        // leaving the medium: reflection and refraction by the exact Fresnel at the interface
        c.specEta = 1.0f / c.specEta;
        float alpha = s.geometry_opacity;
        c.wTrans = alpha;
        c.wTransparent = 1.0f - alpha;
        c.pTrans = alpha;
        c.pTransparent = 1.0f - alpha;
        c.eSpecular = alpha;
        return c;
    }

    // ---- fuzz --------------------------------------------------------------------------------
    c.fuzzRoughness = clamp(s.fuzz_roughness, 0.01f, 1.0f);
    float fuzzAlbedo = OpenPBRFuzzAlbedo(min(mu, 1.0f), c.fuzzRoughness);
    c.fuzzColor = s.fuzz_color * fuzzAlbedo;
    float underFuzz = 1.0f - s.fuzz_weight * fuzzAlbedo; // fuzz transmission is grey

    // ---- coat --------------------------------------------------------------------------------
    c.coatEta = max(s.coat_ior, 1.0e-3f);
    c.coatAlpha = OpenPBRAlpha(s.coat_roughness, s.coat_roughness_anisotropy);
    float coatAlphaIso = sqrt(c.coatAlpha.x * c.coatAlpha.y);
    float coatF0 = OpenPBRF0(c.coatEta);
    c.coatMultiScatter = OpenPBRMultiScatter(coatF0, coatAlphaIso, mu);
    float coatAlbedo = (OpenPBRGGXAlbedo(coatF0, coatAlphaIso, mu) * c.coatMultiScatter).x;
    // absorption along the refracted path, coat_color being the normal-incidence round trip
    float muT = sqrt(saturate(1.0f - (1.0f - mu * mu) / (c.coatEta * c.coatEta)));
    float3 coatAbsorption = pow(max(s.coat_color, 0.0f), 1.0f / max(muT, 1.0e-2f));
    float3 underCoat = lerp(1.0f, coatAbsorption * (1.0f - coatAlbedo), C);

    // ---- base albedos ------------------------------------------------------------------------
    float M = s.base_metalness;
    c.metalF0 = saturate(s.base_weight * s.base_color);
    c.metalTint = s.specular_color;
    c.metalMultiScatter = OpenPBRMultiScatter(c.metalF0, specAlphaIso, mu);
    float3 metalAlbedo = OpenPBRGGXAlbedo(c.metalF0, specAlphaIso, mu) * c.metalMultiScatter * s.specular_weight;

    float specF0 = OpenPBRF0(c.specEta);
    c.specularColor = s.specular_color;
    c.specMultiScatter = OpenPBRMultiScatter(specF0, specAlphaIso, mu);
    float3 specAlbedo = s.specular_color * OpenPBRGGXAlbedo(specF0, specAlphaIso, mu) * c.specMultiScatter;

    c.diffuseColor = saturate(lerp(s.base_weight * s.base_color, s.subsurface_color, s.subsurface_weight));
    c.diffuseRoughness = saturate(s.base_diffuse_roughness);
    float3 diffuseAlbedo = OpenPBREONAlbedo(c.diffuseColor, c.diffuseRoughness, mu);

    // coat darkening: light bouncing back down from under the coat (spec "Coat darkening")
    float K = 1.0f - (1.0f - coatF0) / (c.coatEta * c.coatEta);
    float3 baseAlbedo = lerp(c.diffuseColor, c.metalF0 * s.specular_weight, M);
    float3 darkening = (1.0f - K) / max(1.0f - baseAlbedo * K, 1.0e-3f);
    float3 underCoatDarkened = underCoat * lerp(1.0f, darkening, C * s.coat_darkening);

    // ---- weights -----------------------------------------------------------------------------
    float alpha = s.geometry_opacity;
    float3 base = alpha * underFuzz * underCoatDarkened;
    float T = saturate(s.transmission_weight);
    c.wFuzz = alpha * s.fuzz_weight;
    c.wCoat = alpha * underFuzz * C;
    c.wMetal = base * M;
    c.wSpec = base * (1.0f - M);
    float3 underSpec = base * (1.0f - M) * saturate(1.0f - specAlbedo);
    c.wTrans = underSpec * T * s.transmission_color;
    c.wDiff = underSpec * (1.0f - T);
    c.wTransparent = 1.0f - alpha;

    c.emission = s.emission * alpha * underFuzz * underCoat;

    // ---- selection probabilities, by each lobe's share of the reflected energy -----------------
    float eFuzz = OpenPBRLuminance(c.wFuzz * c.fuzzColor);
    float eCoat = OpenPBRLuminance(c.wCoat * coatAlbedo);
    float eMetal = OpenPBRLuminance(c.wMetal * metalAlbedo);
    float eSpec = OpenPBRLuminance(c.wSpec * specAlbedo);
    float eTrans = OpenPBRLuminance(c.wTrans);
    float eDiff = OpenPBRLuminance(c.wDiff * diffuseAlbedo);
    float eTransparent = c.wTransparent;
    float eSum = eFuzz + eCoat + eMetal + eSpec + eTrans + eDiff + eTransparent;
    float invSum = eSum > 0.0f ? 1.0f / eSum : 0.0f;
    c.pFuzz = eFuzz * invSum;
    c.pCoat = eCoat * invSum;
    c.pMetal = eMetal * invSum;
    c.pSpec = eSpec * invSum;
    c.pTrans = eTrans * invSum;
    c.pDiff = eDiff * invSum;
    c.pTransparent = eTransparent * invSum;

    c.eSpecular = c.wCoat * coatAlbedo + c.wMetal * metalAlbedo + c.wSpec * specAlbedo + c.wTrans;
    c.eDiffuse = c.wDiff * diffuseAlbedo + c.wFuzz * c.fuzzColor;
    return c;
}

// --- per-lobe evaluation: f * |cos(wo)| and the lobe's own pdf ------------------------------

float3 OpenPBREvalFuzz(OpenPBRClosure c, float3 wi, float3 wo, out float pdf)
{
    float x = min(wi.z, 1.0f);
    float aInv = OpenPBRFuzzLTCaInv(x, c.fuzzRoughness);
    float bInv = OpenPBRFuzzLTCbInv(x, c.fuzzRoughness);
    float3 w = mul(OpenPBRFuzzBasis(wi), wo);
    float3 wOrig = float3(aInv * w.x + bInv * w.z, aInv * w.y, w.z);
    float lenSqr = dot(wOrig, wOrig);
    float jacobian = (aInv / lenSqr) * (aInv / lenSqr);
    pdf = max(wOrig.z, 0.0f) * OpenPBRInvPi * jacobian;
    return c.fuzzColor * pdf;
}

// reflection off a GGX interface, Fresnel left to the caller
float OpenPBREvalReflection(float3 wi, float3 wo, float2 alpha, out float3 m, out float pdf)
{
    m = normalize(wi + wo);
    float D = OpenPBRGGXD(m, alpha);
    pdf = OpenPBRGGXG1(wi, alpha) * D / (4.0f * wi.z);
    return D * OpenPBRGGXG2(wi, wo, alpha) / (4.0f * wi.z);
}

// refraction through a GGX interface (Walter 2007), eta = n_out / n_in, Fresnel left to the caller
float OpenPBREvalRefraction(float3 wi, float3 wo, float2 alpha, float eta, out float pdf)
{
    pdf = 0.0f;
    float3 m = normalize(wi + eta * wo);
    if (m.z < 0.0f)
        m = -m;
    float wiDm = dot(wi, m);
    float woDm = dot(wo, m);
    if (wiDm <= 0.0f || woDm >= 0.0f)
        return 0.0f;
    float denominator = wiDm + eta * woDm;
    float jacobian = eta * eta * abs(woDm) / (denominator * denominator);
    float D = OpenPBRGGXD(m, alpha);
    pdf = OpenPBRGGXG1(wi, alpha) * wiDm * D / wi.z * jacobian;
    return wiDm * D * OpenPBRGGXG2(wi, wo, alpha) / wi.z * jacobian;
}

// BSDF * |cos(L)| for a light direction L (unit, world space, from the surface). pdf: the mixture
// pdf OpenPBRSample would have produced L with. Delta lobes (pass-through) are not included.
float3 OpenPBREval(OpenPBRClosure c, float3 L, out float pdf)
{
    pdf = 0.0f;
    float3 wi = c.Vl;
    float3 wo = OpenPBRToLocal(c, L);
    float3 result = 0.0f;
    float lobePdf;
    float3 m;

    if (c.inside)
    {
        if (c.pTrans <= 0.0f)
            return 0.0f;
        if (wo.z > 0.0f)
        {
            float f = OpenPBREvalReflection(wi, wo, c.specAlpha, m, lobePdf);
            float F = OpenPBRFresnelDielectric(dot(wi, m), c.specEta);
            result = c.wTrans * (f * F);
            pdf = c.pTrans * lobePdf * F;
        }
        else if (wo.z < 0.0f)
        {
            float f = OpenPBREvalRefraction(wi, wo, c.specAlpha, c.specEta, lobePdf);
            float3 mt = normalize(wi + c.specEta * wo);
            float F = OpenPBRFresnelDielectric(dot(wi, mt * sign(mt.z)), c.specEta);
            result = c.wTrans * (f * (1.0f - F));
            pdf = c.pTrans * lobePdf * (1.0f - F);
        }
        return result;
    }

    if (wo.z > 0.0f)
    {
        if (c.pFuzz > 0.0f)
        {
            result += c.wFuzz * OpenPBREvalFuzz(c, wi, wo, lobePdf);
            pdf += c.pFuzz * lobePdf;
        }
        if (c.pCoat > 0.0f)
        {
            float f = OpenPBREvalReflection(wi, wo, c.coatAlpha, m, lobePdf);
            result += c.wCoat * c.coatMultiScatter * (f * OpenPBRFresnelDielectric(dot(wi, m), c.coatEta));
            pdf += c.pCoat * lobePdf;
        }
        if (c.pMetal > 0.0f || c.pSpec > 0.0f)
        {
            float f = OpenPBREvalReflection(wi, wo, c.specAlpha, m, lobePdf);
            float wiDm = dot(wi, m);
            result += c.wMetal * c.metalMultiScatter * OpenPBRFresnelF82Tint(wiDm, c.metalF0, c.metalTint) * f;
            result += c.wSpec * c.specMultiScatter * c.specularColor * (OpenPBRFresnelDielectric(wiDm, c.specEta) * f);
            pdf += (c.pMetal + c.pSpec) * lobePdf;
        }
        if (c.pDiff > 0.0f)
        {
            result += c.wDiff * OpenPBREON(c.diffuseColor, c.diffuseRoughness, wi, wo) * wo.z;
            pdf += c.pDiff * wo.z * OpenPBRInvPi;
        }
    }
    else if (wo.z < 0.0f && c.pTrans > 0.0f)
    {
        float f;
        if (c.thinWalled) // a thin sheet: the reflection lobe mirrored through the surface
            f = OpenPBREvalReflection(wi, float3(wo.xy, -wo.z), c.specAlpha, m, lobePdf);
        else
            f = OpenPBREvalRefraction(wi, wo, c.specAlpha, c.specEta, lobePdf);
        result += c.wTrans * f;
        pdf += c.pTrans * lobePdf;
    }
    return result;
}

// Picks a lobe and samples it. weight = BSDF * |cos(L)| / pdf, the throughput factor;
// roughness: the sampled lobe's (1 for diffuse and fuzz, 0 for pass-through).
bool OpenPBRSample(OpenPBRClosure c, float3 u, out float3 L, out float3 weight, out float pdf, out uint flags, out float roughness)
{
    L = 0.0f;
    weight = 0.0f;
    pdf = 0.0f;
    flags = 0;
    roughness = 1.0f;
    float3 wi = c.Vl;
    float3 wo = 0.0f;
    float x = u.z;

    if (x < c.pTransparent) // geometry_opacity: straight through, a delta lobe
    {
        L = -c.V;
        weight = c.wTransparent / c.pTransparent;
        pdf = c.pTransparent;
        flags = OpenPBRSampleTransmission;
        roughness = 0.0f;
        return true;
    }
    x -= c.pTransparent;

    if (x < c.pFuzz)
    {
        float aInv = OpenPBRFuzzLTCaInv(min(wi.z, 1.0f), c.fuzzRoughness);
        float bInv = OpenPBRFuzzLTCbInv(min(wi.z, 1.0f), c.fuzzRoughness);
        float3 wOrig = OpenPBRSampleCosine(u.xy);
        float3 w = float3(wOrig.x / aInv - wOrig.z * bInv / aInv, wOrig.y / aInv, wOrig.z);
        wo = mul(normalize(w), OpenPBRFuzzBasis(wi)); // basis is orthonormal: transpose = inverse
        weight = c.wFuzz * c.fuzzColor / c.pFuzz;     // the LTC lobe is sampled exactly
        OpenPBREvalFuzz(c, wi, wo, pdf);
        pdf *= c.pFuzz;
    }
    else if ((x -= c.pFuzz) < c.pCoat)
    {
        float3 m = OpenPBRSampleGGXVNDF(wi, c.coatAlpha, u.xy);
        wo = reflect(-wi, m);
        if (wo.z <= 0.0f)
            return false;
        float F = OpenPBRFresnelDielectric(dot(wi, m), c.coatEta);
        weight = c.wCoat * c.coatMultiScatter * (F * OpenPBRGGXG2(wi, wo, c.coatAlpha) / OpenPBRGGXG1(wi, c.coatAlpha) / c.pCoat);
        flags = OpenPBRSampleGlossy;
        roughness = sqrt(sqrt(c.coatAlpha.x * c.coatAlpha.y));
        pdf = c.pCoat * OpenPBRGGXG1(wi, c.coatAlpha) * OpenPBRGGXD(m, c.coatAlpha) / (4.0f * wi.z);
    }
    else if ((x -= c.pCoat) < c.pMetal + c.pSpec)
    {
        float3 m = OpenPBRSampleGGXVNDF(wi, c.specAlpha, u.xy);
        wo = reflect(-wi, m);
        if (wo.z <= 0.0f)
            return false;
        float wiDm = dot(wi, m);
        float G = OpenPBRGGXG2(wi, wo, c.specAlpha) / OpenPBRGGXG1(wi, c.specAlpha);
        // metal and dielectric reflection share the NDF: one sample serves both
        float3 F = c.wMetal * c.metalMultiScatter * OpenPBRFresnelF82Tint(wiDm, c.metalF0, c.metalTint)
                 + c.wSpec * c.specMultiScatter * c.specularColor * OpenPBRFresnelDielectric(wiDm, c.specEta);
        weight = F * (G / (c.pMetal + c.pSpec));
        flags = OpenPBRSampleGlossy;
        roughness = c.specRoughness;
        pdf = (c.pMetal + c.pSpec) * OpenPBRGGXG1(wi, c.specAlpha) * OpenPBRGGXD(m, c.specAlpha) / (4.0f * wi.z);
    }
    else if ((x -= c.pMetal + c.pSpec) < c.pTrans)
    {
        float3 m = OpenPBRSampleGGXVNDF(wi, c.specAlpha, u.xy);
        float G1 = OpenPBRGGXG1(wi, c.specAlpha);
        float pdfM = G1 * OpenPBRGGXD(m, c.specAlpha) / wi.z; // * wi.m, cancelled below
        roughness = c.specRoughness;
        if (c.inside)
        {
            // exiting: reflect or refract by the Fresnel term, which then cancels out of the weight
            float wiDm = dot(wi, m);
            float F = OpenPBRFresnelDielectric(wiDm, c.specEta);
            float choice = x / c.pTrans;
            if (choice < F)
            {
                wo = reflect(-wi, m);
                if (wo.z <= 0.0f)
                    return false;
                flags = OpenPBRSampleGlossy;
                pdf = c.pTrans * F * pdfM / 4.0f;
            }
            else
            {
                wo = refract(-wi, m, 1.0f / c.specEta);
                if (wo.z >= 0.0f)
                    return false;
                float denominator = wiDm + c.specEta * dot(wo, m);
                flags = OpenPBRSampleGlossy | OpenPBRSampleTransmission | OpenPBRSampleCrossesMedium;
                pdf = c.pTrans * (1.0f - F) * pdfM * wiDm * c.specEta * c.specEta * abs(dot(wo, m)) / (denominator * denominator);
            }
            weight = c.wTrans * (OpenPBRGGXG2(wi, wo, c.specAlpha) / G1 / c.pTrans);
        }
        else if (c.thinWalled)
        {
            float3 wr = reflect(-wi, m);
            if (wr.z <= 0.0f)
                return false;
            wo = float3(wr.xy, -wr.z);
            weight = c.wTrans * (OpenPBRGGXG2(wi, wr, c.specAlpha) / G1 / c.pTrans);
            flags = OpenPBRSampleGlossy | OpenPBRSampleTransmission;
            pdf = c.pTrans * pdfM / 4.0f;
        }
        else
        {
            // entering: refraction only, the reflected part is the specular lobe above
            wo = refract(-wi, m, 1.0f / c.specEta);
            if (wo.z >= 0.0f) // total internal reflection (specular_ior < 1)
                return false;
            float wiDm = dot(wi, m);
            float denominator = wiDm + c.specEta * dot(wo, m);
            weight = c.wTrans * (OpenPBRGGXG2(wi, wo, c.specAlpha) / G1 / c.pTrans);
            flags = OpenPBRSampleGlossy | OpenPBRSampleTransmission | OpenPBRSampleCrossesMedium;
            pdf = c.pTrans * pdfM * wiDm * c.specEta * c.specEta * abs(dot(wo, m)) / (denominator * denominator);
        }
    }
    else if (c.pDiff > 0.0f)
    {
        wo = OpenPBRSampleCosine(u.xy);
        // EON is close enough to Lambert for cosine sampling: weight = f * cos / (cos / pi)
        weight = c.wDiff * OpenPBREON(c.diffuseColor, c.diffuseRoughness, wi, wo) * (PI / c.pDiff);
        pdf = c.pDiff * wo.z * OpenPBRInvPi;
    }
    else
        return false;

    L = normalize(OpenPBRToWorld(c, wo));
    return any(weight > 0.0f);
}

// A direction from the glossy reflection lobe alone (metal / specular NDF), for the DLSS-RR
// specular hit distance guide.
bool OpenPBRSampleReflectionDirection(OpenPBRClosure c, float2 u, out float3 L)
{
    float3 m = OpenPBRSampleGGXVNDF(c.Vl, c.specAlpha, u);
    float3 wo = reflect(-c.Vl, m);
    L = normalize(OpenPBRToWorld(c, wo));
    return wo.z > 0.0f;
}

// DLSS-RR guides: the directional albedos toward the viewer, split by how RR denoises them.
void OpenPBRGuides(OpenPBRClosure c, out float3 diffuseAlbedo, out float3 specularAlbedo)
{
    diffuseAlbedo = c.eDiffuse;
    specularAlbedo = c.eSpecular;
}

#endif // __OPENPBR__
