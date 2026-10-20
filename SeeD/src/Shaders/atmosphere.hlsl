#pragma once

// Sky atmosphere, after Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering
// Technique" (reference code: github.com/sebh/UnrealEngineSkyAtmosphere). Atmosphere space is in
// km, y up, planet center at the origin: world (x, y, z) meters -> (x, y + bottomRadius*1000, z) / 1000.
// Every LUT is normalized to a sun illuminance of 1; callers scale by atmosphere.sunIlluminance.

#include "structs.hlsl"

#define ATMO_PI 3.14159265358979323846f

static const uint2 TRANSMITTANCE_LUT_SIZE = uint2(256, 64);
static const uint MULTISCATTERING_LUT_SIZE = 32;
static const uint2 SKYVIEW_LUT_SIZE = uint2(192, 108);
static const uint AERIAL_PERSPECTIVE_SIZE = 32;
static const float AERIAL_PERSPECTIVE_KM_PER_SLICE = 4.0f; // 128 km in total, slices squared toward the camera

// Keeps the view point strictly above the ground sphere, so ground intersections stay stable
// at y = 0 (and for cameras below it, caves etc.)
static const float PLANET_RADIUS_OFFSET = 0.01f;

struct MediumSample
{
    float3 scattering;
    float3 extinction;
    float3 scatteringRayleigh;
    float3 scatteringMie;
};

MediumSample SampleMedium(HLSL::Atmosphere atmo, float3 pos)
{
    float altitude = max(length(pos) - atmo.bottomRadius, 0.0f);
    float densityRayleigh = exp(-altitude / atmo.rayleighScattering.w);
    float densityMie = exp(-altitude / atmo.mieScattering.w);
    float densityOzone = saturate(1.0f - abs(altitude - 25.0f) / 15.0f); // tent: 0 at 10 km, 1 at 25 km, 0 at 40 km

    MediumSample s;
    s.scatteringRayleigh = atmo.rayleighScattering.xyz * densityRayleigh;
    s.scatteringMie = atmo.mieScattering.xyz * densityMie;
    s.scattering = s.scatteringRayleigh + s.scatteringMie;
    s.extinction = s.scattering + atmo.mieAbsorption.xyz * densityMie + atmo.ozoneAbsorption.xyz * densityOzone;
    return s;
}

float RayleighPhase(float cosTheta)
{
    return 3.0f / (16.0f * ATMO_PI) * (1.0f + cosTheta * cosTheta);
}

// cosTheta = dot(direction to the sun, direction to the eye's reverse) = dot(sunDir, viewDir): forward peak when looking at the sun
float HenyeyGreensteinPhase(float g, float cosTheta)
{
    float denom = 1.0f + g * g - 2.0f * g * cosTheta;
    return (1.0f - g * g) / (4.0f * ATMO_PI * denom * sqrt(denom));
}

// Nearest positive intersection of a ray with a sphere centered at the origin, -1 if none
float RaySphereNearest(float3 origin, float3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return -1.0f;
    float s = sqrt(discriminant);
    float t0 = -b - s;
    float t1 = -b + s;
    if (t0 >= 0.0f)
        return t0;
    return t1 >= 0.0f ? t1 : -1.0f;
}

// Texel-center remapping so a [0, 1] parameter hits the first and last texel centers exactly
float FromUnitToSubUv(float u, float resolution) { return (u + 0.5f / resolution) * (resolution / (resolution + 1.0f)); }
float FromSubUvToUnit(float u, float resolution) { return (u - 0.5f / resolution) * (resolution / (resolution - 1.0f)); }

// ----------------- TRANSMITTANCE LUT (Bruneton 2017 parametrization) -------------------------
void TransmittanceUvToParams(HLSL::Atmosphere atmo, float2 uv, out float viewHeight, out float viewZenithCos)
{
    float H = sqrt(atmo.topRadius * atmo.topRadius - atmo.bottomRadius * atmo.bottomRadius);
    float rho = H * uv.y;
    viewHeight = sqrt(rho * rho + atmo.bottomRadius * atmo.bottomRadius);

    float dMin = atmo.topRadius - viewHeight;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);
    viewZenithCos = d == 0.0f ? 1.0f : (H * H - rho * rho - d * d) / (2.0f * viewHeight * d);
    viewZenithCos = clamp(viewZenithCos, -1.0f, 1.0f);
}

float2 TransmittanceParamsToUv(HLSL::Atmosphere atmo, float viewHeight, float viewZenithCos)
{
    float H = sqrt(max(0.0f, atmo.topRadius * atmo.topRadius - atmo.bottomRadius * atmo.bottomRadius));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - atmo.bottomRadius * atmo.bottomRadius));

    float discriminant = viewHeight * viewHeight * (viewZenithCos * viewZenithCos - 1.0f) + atmo.topRadius * atmo.topRadius;
    float d = max(0.0f, -viewHeight * viewZenithCos + sqrt(max(discriminant, 0.0f)));

    float dMin = atmo.topRadius - viewHeight;
    float dMax = rho + H;
    return float2((d - dMin) / (dMax - dMin), rho / H);
}

// Transmittance from pos to the top of the atmosphere along dir (ignores the ground: callers test it)
float3 SampleTransmittance(HLSL::Atmosphere atmo, float3 pos, float3 dir)
{
    float viewHeight = length(pos);
    float2 uv = TransmittanceParamsToUv(atmo, viewHeight, dot(dir, pos / viewHeight));
    Texture2D<float4> lut = ResourceDescriptorHeap[atmo.transmittanceLUT];
    return lut.SampleLevel(samplerLinearClamp, uv, 0).rgb;
}

// ----------------- MULTIPLE SCATTERING LUT --------------------------------------------------
// (x: sun zenith cos in [-1, 1], y: altitude in [0, 1] of the atmosphere thickness), contains the
// isotropic multiple scattering transfer Psi_ms (paper eq. 10), multiplied by the local scattering
float3 SampleMultiScattering(HLSL::Atmosphere atmo, float3 pos, float sunZenithCos)
{
    float2 uv = saturate(float2(sunZenithCos * 0.5f + 0.5f, (length(pos) - atmo.bottomRadius) / (atmo.topRadius - atmo.bottomRadius)));
    uv = float2(FromUnitToSubUv(uv.x, MULTISCATTERING_LUT_SIZE), FromUnitToSubUv(uv.y, MULTISCATTERING_LUT_SIZE));
    Texture2D<float4> lut = ResourceDescriptorHeap[atmo.multiScatteringLUT];
    return lut.SampleLevel(samplerLinearClamp, uv, 0).rgb;
}

// ----------------- RAY MARCHING -------------------------------------------------------------
struct ScatteringResult
{
    float3 L;               // in-scattered luminance for a unit sun illuminance
    float3 opticalDepth;
    float3 multiScatAs1;    // used by the multiple scattering LUT only: f_ms before the 1/4pi phase
};

// Moves pos onto the top of the atmosphere when it starts outside; false if the ray misses it
bool MoveToTopAtmosphere(HLSL::Atmosphere atmo, inout float3 pos, float3 dir)
{
    float viewHeight = length(pos);
    if (viewHeight > atmo.topRadius)
    {
        float t = RaySphereNearest(pos, dir, atmo.topRadius);
        if (t < 0.0f)
            return false;
        pos += dir * (t + 0.001f);
    }
    return true;
}

// isotropicPhase: true for the multiple scattering LUT (paper section 5.5.2), false for the sky
// multipleScattering: add Psi_ms from the LUT (needs it built), ground: add the lit ground at the ray's end
ScatteringResult IntegrateScatteredLuminance(HLSL::Atmosphere atmo, float3 pos, float3 dir, float3 sunDir,
    uint sampleCount, bool isotropicPhase, bool multipleScattering, bool ground, float tMaxMax)
{
    ScatteringResult result = (ScatteringResult)0;

    float tBottom = RaySphereNearest(pos, dir, atmo.bottomRadius);
    float tTop = RaySphereNearest(pos, dir, atmo.topRadius);
    float tMax;
    if (tBottom < 0.0f)
    {
        if (tTop < 0.0f)
            return result; // no intersection with the atmosphere at all
        tMax = tTop;
    }
    else
        tMax = tTop > 0.0f ? min(tTop, tBottom) : tBottom;
    tMax = min(tMax, tMaxMax);

    float cosTheta = dot(sunDir, dir);
    float miePhase = isotropicPhase ? 1.0f / (4.0f * ATMO_PI) : HenyeyGreensteinPhase(atmo.mieAbsorption.w, cosTheta);
    float rayleighPhase = isotropicPhase ? 1.0f / (4.0f * ATMO_PI) : RayleighPhase(cosTheta);

    float3 throughput = 1.0f;
    float dt = tMax / sampleCount;
    for (uint i = 0; i < sampleCount; i++)
    {
        // mid-point of each segment
        float t = dt * (i + 0.5f);
        float3 p = pos + t * dir;

        MediumSample medium = SampleMedium(atmo, p);
        float3 sampleOpticalDepth = medium.extinction * dt;
        float3 sampleTransmittance = exp(-sampleOpticalDepth);
        result.opticalDepth += sampleOpticalDepth;

        float pHeight = length(p);
        float3 up = p / pHeight;
        float sunZenithCos = dot(sunDir, up);
        float3 transmittanceToSun = SampleTransmittance(atmo, p, sunDir);

        float3 phaseTimesScattering = medium.scatteringMie * miePhase + medium.scatteringRayleigh * rayleighPhase;
        float3 multiScattered = multipleScattering ? SampleMultiScattering(atmo, p, sunZenithCos) : 0.0f;

        // planet shadow
        float tEarth = RaySphereNearest(p, sunDir, atmo.bottomRadius - PLANET_RADIUS_OFFSET);
        float earthShadow = tEarth >= 0.0f ? 0.0f : 1.0f;

        float3 S = earthShadow * transmittanceToSun * phaseTimesScattering + multiScattered * medium.scattering;

        // Energy conserving analytical integration over the segment (Hillaire 2015, Frostbite)
        float3 invExtinction = 1.0f / max(medium.extinction, 1e-9f);
        result.multiScatAs1 += throughput * (medium.scattering - medium.scattering * sampleTransmittance) * invExtinction;
        result.L += throughput * (S - S * sampleTransmittance) * invExtinction;
        throughput *= sampleTransmittance;
    }

    if (ground && tMax == tBottom && tBottom > 0.0f)
    {
        float3 p = pos + tBottom * dir;
        float3 up = normalize(p);
        float3 transmittanceToSun = SampleTransmittance(atmo, p, sunDir);
        float NdotL = saturate(dot(up, sunDir));
        result.L += transmittanceToSun * throughput * NdotL * atmo.groundAlbedo.xyz / ATMO_PI;
    }

    return result;
}

// ----------------- SKY VIEW LUT -------------------------------------------------------------
// (x: azimuth to the sun, squeezed toward the sun side, y: zenith angle, non-linear with the
// horizon at v = 0.5 so its sharp gradient gets most of the resolution)
void SkyViewUvToParams(HLSL::Atmosphere atmo, float2 uv, float viewHeight, out float viewZenithCos, out float lightViewCos)
{
    uv = float2(FromSubUvToUnit(uv.x, SKYVIEW_LUT_SIZE.x), FromSubUvToUnit(uv.y, SKYVIEW_LUT_SIZE.y));

    float vHorizon = sqrt(max(viewHeight * viewHeight - atmo.bottomRadius * atmo.bottomRadius, 0.0f));
    float beta = acos(vHorizon / viewHeight);
    float zenithHorizonAngle = ATMO_PI - beta;

    if (uv.y < 0.5f)
    {
        float coord = 1.0f - 2.0f * uv.y;
        coord = 1.0f - coord * coord;
        viewZenithCos = cos(zenithHorizonAngle * coord);
    }
    else
    {
        float coord = uv.y * 2.0f - 1.0f;
        viewZenithCos = cos(zenithHorizonAngle + beta * coord * coord);
    }

    lightViewCos = -(uv.x * uv.x * 2.0f - 1.0f);
}

float2 SkyViewParamsToUv(HLSL::Atmosphere atmo, bool intersectGround, float viewZenithCos, float lightViewCos, float viewHeight)
{
    float vHorizon = sqrt(max(viewHeight * viewHeight - atmo.bottomRadius * atmo.bottomRadius, 0.0f));
    float beta = acos(vHorizon / viewHeight);
    float zenithHorizonAngle = ATMO_PI - beta;

    float2 uv;
    if (!intersectGround)
    {
        float coord = acos(viewZenithCos) / zenithHorizonAngle;
        uv.y = (1.0f - sqrt(max(1.0f - coord, 0.0f))) * 0.5f;
    }
    else
    {
        float coord = (acos(viewZenithCos) - zenithHorizonAngle) / beta;
        uv.y = sqrt(max(coord, 0.0f)) * 0.5f + 0.5f;
    }
    uv.x = sqrt(-lightViewCos * 0.5f + 0.5f);

    return float2(FromUnitToSubUv(uv.x, SKYVIEW_LUT_SIZE.x), FromUnitToSubUv(uv.y, SKYVIEW_LUT_SIZE.y));
}

// World meters -> atmosphere km
float3 AtmospherePosition(HLSL::Atmosphere atmo, float3 worldPos)
{
    float3 p = worldPos * 0.001f;
    p.y = max(p.y, 0.0f) + atmo.bottomRadius + PLANET_RADIUS_OFFSET;
    return p;
}

// Sky luminance seen from the camera along dir (sun disk excluded), sun illuminance applied
float3 SkyViewLuminance(HLSL::Atmosphere atmo, float3 cameraWorldPos, float3 dir)
{
    if (atmo.skyViewLUT == HLSL::invalidUINT || atmo.sunIlluminance.w == 0.0f)
        return 0.0f;

    // The LUT is built at the camera's altitude with a flat-earth up vector: horizontal camera
    // motion is negligible against the planet radius
    float viewHeight = AtmospherePosition(atmo, cameraWorldPos).y;
    float3 up = float3(0, 1, 0);
    float viewZenithCos = dot(dir, up);

    float3 viewHorizontal = dir - up * viewZenithCos;
    float3 sunHorizontal = atmo.sunDirection.xyz - up * dot(atmo.sunDirection.xyz, up);
    float lightViewCos = 1.0f;
    if (dot(viewHorizontal, viewHorizontal) > 1e-8f && dot(sunHorizontal, sunHorizontal) > 1e-8f)
        lightViewCos = dot(normalize(viewHorizontal), normalize(sunHorizontal));

    bool intersectGround = RaySphereNearest(float3(0, viewHeight, 0), dir, atmo.bottomRadius) >= 0.0f;
    float2 uv = SkyViewParamsToUv(atmo, intersectGround, viewZenithCos, lightViewCos, viewHeight);

    Texture2D<float4> lut = ResourceDescriptorHeap[atmo.skyViewLUT];
    return lut.SampleLevel(samplerLinearClamp, uv, 0).rgb * atmo.sunIlluminance.xyz;
}

// ----------------- AERIAL PERSPECTIVE ------------------------------------------------------
// Slice index (continuous) <-> view distance along the ray in km. The volume's w coordinate is
// sqrt(slice / size): slices are squared toward the camera
float AerialPerspectiveSliceToKm(float slice) { return slice * AERIAL_PERSPECTIVE_KM_PER_SLICE; }

// xyz: atmosphere in-scatter between the camera and the point (sun applied), w: transmittance
// screenUV: the pixel's [0, 1] uv in the camera frustum, viewDistance: meters along the view ray
float4 SampleAerialPerspective(HLSL::Atmosphere atmo, float2 screenUV, float viewDistance)
{
    if (atmo.aerialPerspectiveLUT == HLSL::invalidUINT || atmo.sunIlluminance.w == 0.0f)
        return float4(0, 0, 0, 1);

    float slice = viewDistance * 0.001f / AERIAL_PERSPECTIVE_KM_PER_SLICE;
    // the first slice center is not at the camera: fade the first half-slice to nothing
    float weight = 1.0f;
    if (slice < 0.5f)
    {
        weight = saturate(slice * 2.0f);
        slice = 0.5f;
    }
    float w = sqrt(slice / AERIAL_PERSPECTIVE_SIZE);

    Texture3D<float4> lut = ResourceDescriptorHeap[atmo.aerialPerspectiveLUT];
    float4 ap = lut.SampleLevel(samplerLinearClamp, float3(screenUV, w), 0) * weight;
    return float4(ap.rgb, 1.0f - ap.a);
}

// Radiance of the sun disk seen from pos along dir, attenuated by the atmosphere; 0 outside the disk
float3 SunDiskLuminance(HLSL::Atmosphere atmo, float3 worldPos, float3 dir)
{
    if (atmo.transmittanceLUT == HLSL::invalidUINT || atmo.sunIlluminance.w == 0.0f)
        return 0.0f;

    float angularRadius = max(atmo.sunDirection.w, 0.0005f);
    float cosRadius = cos(angularRadius);
    if (dot(dir, atmo.sunDirection.xyz) < cosRadius)
        return 0.0f;

    float3 p = AtmospherePosition(atmo, worldPos);
    if (RaySphereNearest(p, dir, atmo.bottomRadius) >= 0.0f)
        return 0.0f;

    // uniform disk: illuminance / solid angle
    float solidAngle = 2.0f * ATMO_PI * (1.0f - cosRadius);
    return SampleTransmittance(atmo, p, dir) * atmo.sunIlluminance.xyz / solidAngle;
}
