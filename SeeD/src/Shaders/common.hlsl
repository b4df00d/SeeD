#pragma once

#include "binding.hlsl"
#include "structs.hlsl"

static float4 complexity[10] =
{
#if 0
	float4(0.0,1.0,0.127,1.0),
	float4(0.0,1.0,0.0,1.0),
	float4(0.046,0.52,0.0,1.0),
	float4(0.215,0.215,0.0,1.0),
	float4(0.52,0.046,0.0,1.0),
	float4(0.7,0.0,0.0,1.0),
	float4(1.0,0.0,0.0,1.0),
	float4(1.0,0.0,0.5,1.0),
	float4(1.0,0.9,0.9,1.0),
	float4(1.0,1.0,1.0,1.0)
#else
    float4(0.0f / 255.0f, 2.0f / 255.0f, 91.0f / 255.0f, 1),
	float4(0.0f / 255.0f, 108.0f / 255.0f, 251.0f / 255.0f, 1),
	float4(0.0f / 255.0f, 221.0f / 255.0f, 221.0f / 255.0f, 1),
	float4(51.0f / 255.0f, 221.0f / 255.0f, 0.0f / 255.0f, 1),
	float4(255.0f / 255.0f, 252.0f / 255.0f, 0.0f / 255.0f, 1),
	float4(255.0f / 255.0f, 180.0f / 255.0f, 0.0f / 255.0f, 1),
	float4(255.0f / 255.0f, 104.0f / 255.0f, 0.0f / 255.0f, 1),
	float4(226.0f / 255.0f, 22.0f / 255.0f, 0.0f / 255.0f, 1),
	float4(191.0f / 255.0f, 0.0f / 255.0f, 83.0f / 255.0f, 1),
	float4(145.0f / 255.0f, 0.0f / 255.0f, 65.0f / 255.0f, 1)
#endif
};

float4 QuaternionFromMatrix(const in float3x3 mat)
{
    float4 quat;
    float trace = mat[0][0] + mat[1][1] + mat[2][2] + 1.0f;

    if (trace > 1.0f)
    {
        float k = sqrt(trace) * 2.0f;
        quat.x = mat[2][1] - mat[1][2];
        quat.y = mat[0][2] - mat[2][0];
        quat.z = mat[1][0] - mat[0][1];
        quat.w = trace;

        quat /= k;
    }
    else
    {
        uint x = mat[1][1] > mat[0][0] ? 1 : 0;
        if (mat[2][2] > mat[x][x])
            x = 2;

        const float next[3] = { 1, 2, 0 };
        uint y = next[x];
        uint z = next[y];

        trace = mat[x][x] - mat[y][y] - mat[z][z] + 1.0f;
        float k = sqrt(trace) * 2;

        float tmp[3];
        tmp[x] = trace;
        tmp[y] = mat[y][x] + mat[x][y];
        tmp[z] = mat[z][x] + mat[x][z];

        quat.x = tmp[0];
        quat.y = tmp[1];
        quat.z = tmp[2];
        quat.w = mat[z][y] - mat[y][z];

        quat /= k;
    }
    return quat;
}

float3 Max3(float a, float b, float c)
{
    return max(max(a, b), c);
}

float pow3(float x)
{
    return x * x * x;
}

float dot2(float3 p)
{
    return dot(p, p);
}

float3 Saturation(float3 input, float strength)
{
    const float3 LuminanceWeights = float3(0.299, 0.587, 0.114);
    float luminance = dot(input, LuminanceWeights);
    return lerp(luminance, input, strength);
}

// ACES tone mapping curve fit to go from HDR to LDR
//https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

float3 WorldNormal(float3 nrm, float3x3 tbn, float scale)
{
	//nrm = float3(0.5, 0.5, 1);
    float3 tangentSpaceNorm = (nrm * 2 - 1) * scale;
    tangentSpaceNorm.z = 1 - sqrt(tangentSpaceNorm.x * tangentSpaceNorm.x + tangentSpaceNorm.y * tangentSpaceNorm.y);
    float3 worldSpaceNorm = mul(tangentSpaceNorm, tbn);
    return normalize(worldSpaceNorm);
}

// -------- RAND AND NOISE ------------------
//note: uniformly distributed, normalized rand, [0;1[
float Rand(float n)
{
    return frac(sin(dot(n.xx, float2(12.9898, 78.233))) * 43758.5453);
}

float3 Rand(float n, float seed)
{
    float a = Rand(n + seed);
    float b = Rand(n + seed - 0.1);
    float c = Rand(n + seed + 0.2);
    return float3(a, b, c);
}

float3 RandUINT(in uint seed)
{
    uint x = seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    float t = sin(x * .000000215453);

    float r = frac(t * 47582.415453);
    float g = frac(t * 14378.5453);
    float b = frac(t * 4375.95453);

    return float3(r, g, b);
}

float Rand(in float3 seed)
{
    uint x = frac(seed.x) * frac(seed.y) * frac(seed.z) * 503285920;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    float t = sin(x * .000000215453);
    return t;

    float r = (t * 1.415453);
    float g = (t * 2.5453);
    float b = (t * 3.95453);

    return frac(r + g + b);

	/*
	float noiseX = (frac(sin(dot(seed.xy, float2(12.9898, 78.233))) * 43758.5453));
	float noiseY = (frac(sin(dot(seed.yz, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
	float noiseZ = (frac(sin(dot(seed.zx, float2(12.9898, 78.233) * 3.0)) * 43758.5453));
	return frac(noiseX + noiseY + noiseZ);
	*/
}

// https://www.shadertoy.com/view/wssfDl
// this noise, including the 5.58... scrolling constant are from Jorge Jimenez
float InterleavedGradientNoise(float2 pixel, int frame)
{
    pixel += (float(frame % 8) * 5.588238f);
    return frac(52.9829189f * frac(0.06711056f * float(pixel.x) + 0.00583715f * float(pixel.y)));
}


float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

//function to generate a hammersly low discrepency sequence for importance sampling
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}


// Generates a seed for a random number generator from 2 inputs plus a backoff
uint initRand(uint val0, uint val1, uint backoff = 16)
{
    uint v0 = val0, v1 = val1, s0 = 0;

    [unroll]
    for (uint n = 0; n < backoff; n++)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }
    return v0;
}

// Takes our seed, updates it, and returns a pseudorandom float in [0..1]
float nextRand(inout uint s)
{
    s = (1664525u * s + 1013904223u);
    return float(s & 0x00FFFFFF) / float(0x01000000);
}

// ------------------ DEPTH ----------------------------------
/*
// convert a point from post-projection space into view space
float3 ConvertProjToView(float4 p)
{
    p = mul(p, camera.ipMat);
    return (p / p.w).xyz;
}
// convert a depth value from post-projection space into view space
float ConvertProjDepthToView(float z)
{
    return (1.f / (z * camera.ipMat._34 + camera.ipMat._44));
}

float4 WorldPosFromDepth(float d, float2 uvCoord)
{
    float2 screenPos = uvCoord * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

	// Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, d, 1), camera.ipvMat);
    world.xyz /= world.w;
    return world;
}
//Returns World Position of a pixel from clipspace depth map
float4 WorldPosFromDepth(Texture2D<float> depth, float2 uvCoord, float2 resolution)
{
    float d = depth.SampleLevel(pointSampler, uvCoord, 0);
    return WorldPosFromDepth(d, uvCoord);
}

float4 WorldPosFromDepth(RWTexture2D<float> depth, float2 uvCoord)
{
    float d = depth.Load(int2(uvCoord));
    return WorldPosFromDepth(d, uvCoord);
}

float linearZ(float z, float n, float f)
{
    return (n) / (f + z * (n - f));
}

float linearZ01(float z, float n, float f)
{
    return linearZ(z, n, f) / f;
}

float absolutZ(float z, float n, float f)
{
    return (n / z - f) / (n - f);
}
*/

// ----------------- FROXEL ---------------------------------------------
/*
uint ZToFroxel(float linearZ)
{
    float A = linearZ; // 1/(camera.camClipsExt.x* nonLinearZ + camera.camClipsExt.y); // this is to linearize the z but it is already fed
    float B = -log2(A);
    float C = B * camera.camClipsExt.z + camera.camClipsExt.w;
    return uint(max(0.0, C));
	//return uint(max(0.0, log2(1/(camera.camClipsExt.x * linearZ + camera.camClipsExt.y)) * camera.camClipsExt.z + camera.camClipsExt.w));
}

float FroxelToZ(float froxelZ, float specialNear, float far, int count)
{
    if (froxelZ < 1)
        return 0;
    return exp2((froxelZ - count) * (-log2(specialNear / far) / (count - 1)));
}

float3 FroxelToWorld(float3 froxel)
{
    float4 clipPos = float4(froxel / float3(ATMO_VOLUME_SIZE_X, ATMO_VOLUME_SIZE_Y, ATMO_VOLUME_SIZE_Z), 1);
    clipPos.xy = clipPos.xy * 2 - 1;
    float4 worldPos = mul(clipPos, camera.ipMat);
    worldPos = worldPos / worldPos.w;
    worldPos = mul(worldPos, camera.vMat);

    float3 viewDir = worldPos.xyz - camera.camWorldPos.xyz;
    float viewDist = length(viewDir);
    viewDir = viewDir / viewDist;

    viewDist = FroxelToZ(froxel.z, ATMO_VOLUME_SPECIAL_NEAR, camera.camClips.y, ATMO_VOLUME_SIZE_Z) * camera.camClips.y;

    worldPos.xyz = camera.camWorldPos.xyz + viewDir * viewDist;

    return worldPos.xyz;
}

float3 WorldToFroxel(float3 pixelWorldPos)
{
    float4 uvw = mul(float4(pixelWorldPos, 1), camera.ivpMat);
    uvw = uvw / uvw.w;
    uvw.xy = uvw.xy * 0.5 + 0.5;
    //uvw.y = 1 - uvw.y;
    //uvw.xy *= float2(ATMO_VOLUME_SIZE_X, ATMO_VOLUME_SIZE_Y);
    float linearDepth = length(pixelWorldPos - camera.camWorldPos.xyz) / camera.camClips.y;
    uvw.z = ZToFroxel(linearDepth);

    return uvw.xyz;
}

float3 NDCToFroxel(float3 NDC, float3 pixelWorldPos)
{
    float3 uvw = NDC;
    uvw.xy /= globals.screenResolution.xy;
    uvw.y = 1 - uvw.y;
    uvw.xy *= float2(ATMO_VOLUME_SIZE_X, ATMO_VOLUME_SIZE_Y);
    float linearDepth = length(pixelWorldPos - camera.camWorldPos.xyz) / camera.camClips.y;
    uvw.z = ZToFroxel(linearDepth);

    return uvw;
}
*/

// ----------------- LIGHTING ---------------------------------------------
float DistanceFromTriangle(in float3 v1, in float3 v2, in float3 v3, in float3 p)
{
	// prepare data    
    float3 v21 = v2 - v1;
    float3 p1 = p - v1;
    float3 v32 = v3 - v2;
    float3 p2 = p - v2;
    float3 v13 = v1 - v3;
    float3 p3 = p - v3;
    float3 nor = cross(v21, v13);

    return sqrt( // inside/outside test    
		(sign(dot(cross(v21, nor), p1)) +
			sign(dot(cross(v32, nor), p2)) +
			sign(dot(cross(v13, nor), p3)) < 2.0)
		?
		// 3 edges    
		min(min(
			dot2(v21 * saturate(dot(v21, p1) / dot2(v21)) - p1),
			dot2(v32 * saturate(dot(v32, p2) / dot2(v32)) - p2)),
			dot2(v13 * saturate(dot(v13, p3) / dot2(v13)) - p3))
		:
		// 1 face    
		dot(nor, p1) * dot(nor, p1) / dot2(nor)
	);
}

float4 PlaneFromPoints(float3 A, float3 B, float3 C)
{
    float3 N = normalize(cross(B - A, C - A));
    float d = -dot(N, A);
    return float4(N, d);
}

float3 ConstrainToSegment(float3 position, float3 a, float3 b)
{
    float3 ba = b - a;
    float t = dot(position - a, ba) / dot(ba, ba);
    return lerp(a, b, saturate(t));
}

float3 ClosestPointOnLineToSegment(float3 segA, float3 segB, float3 segC, float3 segD)
{
    float3 segDC = segD - segC;
    float lineDirSqrMag = dot(segDC, segDC);
    float3 inPlaneA = segA - ((dot(segA - segC, segDC) / lineDirSqrMag) * segDC);
    float3 inPlaneB = segB - ((dot(segB - segC, segDC) / lineDirSqrMag) * segDC);
    float3 inPlaneBA = inPlaneB - inPlaneA;
    float t = dot(segC - inPlaneA, inPlaneBA) / dot(inPlaneBA, inPlaneBA);
	//t = (inPlaneA != inPlaneB) ? t : 0f; // Zero's t if parallel
    float3 segABtoLineCD = lerp(segA, segB, saturate(t));

    return ConstrainToSegment(segABtoLineCD, segC, segD);
	//return constrainToSegment(segCDtoSegAB, segA, segB);
}

float3 ClosestPointPointCapsule(float3 position, float3 a, float3 b, float r)
{
    float3 pa = position - a;
    float3 ba = b - a;
    float t = saturate(dot(pa, ba) / dot(ba, ba));
    float3 h = lerp(a, b, t);
    float3 ph = h - position;
    float phDist = length(ph);
    ph = ph / phDist;
    float3 p = position + ph * (phDist - r);
    return p;
}

float3 ClosestPointOnPlane(float3 position, float3 a, float3 b, float3 c)
{
    float4 plane = PlaneFromPoints(a, b, c);
    float3 p = position - plane.xyz * dot(position - a, plane.xyz);
    return p;
}

float3 ClosestPointToTriangle(in float3 v1, in float3 v2, in float3 v3, in float3 p)
{
	// prepare data    
    float3 v21 = v2 - v1;
    float3 p1 = p - v1;
    float3 v32 = v3 - v2;
    float3 p2 = p - v2;
    float3 v13 = v1 - v3;
    float3 p3 = p - v3;
    float3 nor = cross(v21, v13);

    float3 q = ClosestPointOnPlane(p, v1, v2, v3);

    if (sign(dot(cross(v21, nor), p1)) +
		sign(dot(cross(v32, nor), p2)) +
		sign(dot(cross(v13, nor), p3)) < 2.0)
    {
		//outside triangle
        float3 q1 = ConstrainToSegment(p, v2, v1);
        float3 q2 = ConstrainToSegment(p, v3, v2);
        float3 q3 = ConstrainToSegment(p, v1, v3);

        float sqDist1 = dot(p, q1);
        float sqDist2 = dot(p, q2);
        float sqDist3 = dot(p, q3);

        if (sqDist1 < sqDist2 && sqDist1 < sqDist3)
            return q1;
        else if (sqDist2 < sqDist1 && sqDist2 < sqDist3)
            return q2;
        else
            return q3;
    }
    else
    {
		// in triangle
        return q;
    }
}

float3 BoxCubeMapLookup(float3 rayOrigin, float3 unitRayDir, float3 boxCenter, float3 boxExtents)
{
	// Based on slab method as described in Real-Time Rendering
	// 16.7.1 (3rd edition).
	// Make relative to the box center.
    float3 p = rayOrigin - boxCenter;

	// The ith slab ray/plane intersection formulas for AABB are:

	// t1 = (-dot(n_i, p) + h_i)/dot(n_i, d) = (-p_i + h_i)/d_i
	// t2 = (-dot(n_i, p) - h_i)/dot(n_i, d) = (-p_i - h_i)/d_i
	// 
	// Vectorize and do ray/plane formulas for every slab together.

    float3 t1 = (-p + boxExtents) / unitRayDir;
    float3 t2 = (-p - boxExtents) / unitRayDir;
	
	// Find max for each coordinate. Because we assume the ray is inside
	// the box, we only want the max intersection parameter.
    float3 tmax = max(t1, t2);

	// Take minimum of all the tmax components:
    float t = min(min(tmax.x, tmax.y), tmax.z);

	// This is relative to the box center so it can be used as a
	// cube map lookup vector.
    return p + t * unitRayDir;

}

//https://gist.github.com/JarkkoPFC/1186bc8a861dae3c8339b0cda4e6cdb3
float4 sphere_screen_extents(in const float3 pos_, in const float rad_, in const float4x4 v2p_)
{
  // calculate horizontal extents
    //assert(v2p_.z.w == 1 && v2p_.w.w == 0);
    float4 res;
    float rad2 = rad_ * rad_, d = pos_.z * rad_;
    float hv = sqrt(pos_.x * pos_.x + pos_.z * pos_.z - rad2);
    float ha = pos_.x * hv, hb = pos_.x * rad_, hc = pos_.z * hv;
    res.x = (ha - d) * v2p_[0][0] / (hc + hb); // left
    res.z = (ha + d) * v2p_[0][0] / (hc - hb); // right

  // calculate vertical extents
    float vv = sqrt(pos_.y * pos_.y + pos_.z * pos_.z - rad2);
    float va = pos_.y * vv, vb = pos_.y * rad_, vc = pos_.z * vv;
    res.y = (va - d) * v2p_[1][1] / (vc + vb); // bottom
    res.w = (va + d) * v2p_[1][1] / (vc - vb); // top
    return res;
}

float4 ScreenSpaceBB(in HLSL::Camera camera, float4 boundingSphere)
{
    //boundingSphere = float4(mul(camera.view, float4(boundingSphere.xyz, 1)).xyz, boundingSphere.w);
    return sphere_screen_extents(boundingSphere.xyz, boundingSphere.w, camera.proj);
}

bool FrustumCulling(in HLSL::Camera camera, float4 boundingSphere)
{
    bool culled = false;
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        float distance = dot(camera.planes[i].xyz, boundingSphere.xyz) + camera.planes[i].w;
        culled |= distance < -boundingSphere.w;
    }
    culled &= distance(camera.worldPos.xyz, boundingSphere.xyz) > boundingSphere.w;
    return culled;
}

//https://youtu.be/EtX7WnFhxtQ?si=eUxHtsi2rsHYCWWC&t=1491  Erik Jansson - GPU driven Rendering with Mesh Shaders in Alan Wake 2
// il y a 4 sample plutot que 1 sample du mip du dessus car les 4 corners peuvent etre a cheval entre 2 pixel du sample du dessus
bool OcclusionCulling(in HLSL::Camera camera, float4 boundingSphere)
{
    if (distance(boundingSphere.xyz, camera.worldPos.xyz) < boundingSphere.w) return false; // BS intersecting the camera pos
    
    float4 viewSphere = float4(mul(camera.view, float4(boundingSphere.xyz, 1)).xyz, boundingSphere.w); // assume view matrix is not scaled
    
    if (boundingSphere.w / saturate(viewSphere.z) < 0.0025f) return true; // BS too small
    
    float3 sphereClosestPointToCamera = viewSphere.xyz - normalize(viewSphere.xyz) * boundingSphere.w;
    float4 clipSphere = mul(camera.proj, float4(sphereClosestPointToCamera.xyz, 1));
    float fBoundSphereDepth = saturate(clipSphere.z / clipSphere.w - 0.001);
    
    float3 vHZB = float3(cullingContext.resolution.x, cullingContext.resolution.y, cullingContext.HZBMipCount);
    Texture2D<float> tHZB = ResourceDescriptorHeap[cullingContext.HZB];
    float4 vLBRT = ScreenSpaceBB(camera, viewSphere);
    
    float4 vToUV = float4(0.5f, -0.5f, 0.5f, -0.5f);
    float4 vUV = saturate(vLBRT.xwzy * vToUV + 0.5f);
    float4 vAABB = vUV * vHZB.xyxy;
    float2 vExtents = vAABB.zw - vAABB.xy;
    
    float fMipLevel = ceil(log2(max(vExtents.x, vExtents.y)));
    fMipLevel = clamp(fMipLevel, 0.0f, vHZB.z - 1.0f);
    
    float4 vOcclusionDepth = float4(tHZB.SampleLevel(samplerPointClamp, vUV.xy, fMipLevel),
                                    tHZB.SampleLevel(samplerPointClamp, vUV.zy, fMipLevel),
                                    tHZB.SampleLevel(samplerPointClamp, vUV.zw, fMipLevel),
                                    tHZB.SampleLevel(samplerPointClamp, vUV.xw, fMipLevel));
    
    float fMaxOcclusionDepth = max(max(max(vOcclusionDepth.x, vOcclusionDepth.y), vOcclusionDepth.z), vOcclusionDepth.w);
    bool bCulled = fMaxOcclusionDepth < fBoundSphereDepth;
    
    return bCulled;
}

/*
LightComp GetLightComp(in uint globalLightIndex, float3 normal, float3 position, float clipZ, float3 viewDir, bool shadows = true)
{
	//https://www.shadertoy.com/view/ldfGWs
	//https://www.gamedev.net/forums/topic/649245-spherical-area-lights/
	//https://www.shadertoy.com/view/lsfGDN
	//https://wickedengine.net/2017/09/07/area-lights/

    Light currentLight = lights[globals.lightHeapIndex][globalLightIndex];

    LightComp l;
    l.color = currentLight.color;
    l.attenuation = 1;
    l.dir = viewDir;
    l.dist = 1;
    l.ndotl = 1;
    l.reflDir = l.dir;
    l.reflDist = l.dist;
    if (currentLight.lightType == 1) // directionnal
    {
        l.dir = -currentLight.lightsPos.xyz;
        l.dist = 1;
        if (shadows && currentLight.shadowIndex != -1)
        {
            l.attenuation = GetShadow(globalLightIndex, position, clipZ);
        }

        l.ndotl = saturate(dot(normal, l.dir));

        l.reflDir = l.dir;
        l.reflDist = l.dist;

		//l.color *= 0.01;
    }
    else if (currentLight.lightType == 2) // omni
    {
        l.dir = currentLight.lightsPos.xyz - position.xyz;
        l.dist = length(l.dir);
        if (shadows && currentLight.shadowIndex != -1)
        {
            l.attenuation = GetOmniShadow(globalLightIndex, -l.dir, l.dist);
        }

        l.dir = l.dir / l.dist;
        l.ndotl = saturate(dot(normal, l.dir));

        l.reflDir = l.dir;
        l.reflDist = l.dist;

        l.color *= 1 / (l.dist * l.dist + 0.01);
        l.color *= (currentLight.lightsPos.w - l.dist) / currentLight.lightsPos.w;
    }
    else if (currentLight.lightType == 5) // sphere
    {
        l.dir = currentLight.lightsPos.xyz - position.xyz;
        l.dist = length(l.dir);
        if (shadows && currentLight.shadowIndex != -1)
        {
            l.attenuation = GetOmniShadow(globalLightIndex, -l.dir, l.dist);
        }

        l.dir = l.dir - (l.dir / l.dist * currentLight.color.a);
        l.dist = length(l.dir);
        l.dir = l.dir / l.dist;
        l.ndotl = saturate(dot(normal, l.dir));

        l.reflDir = (currentLight.lightsPos.xyz - position.xyz);
        float3 R = reflect(viewDir, normal);
        float3 centerToRay = dot(l.reflDir, R) * R - l.reflDir;
        l.reflDir = l.reflDir + centerToRay * saturate(currentLight.color.a / length(centerToRay));
        l.reflDist = length(l.reflDir);
        l.reflDir = l.reflDir / l.reflDist;

        l.color *= 1 / (l.dist * l.dist + 0.01);
		//l.color *= (currentLight.lightsPos.w - l.dist) / currentLight.lightsPos.w;
    }
    else if (currentLight.lightType == 7) // tube
    {
        float3 p = ClosestPointPointCapsule(position, currentLight.lightsPos.xyz, currentLight.lightsPosEnd.xyz, currentLight.lightsPos.w);
        l.dir = p - position;
        l.dist = length(l.dir);
        l.dir = l.dir / l.dist;
        l.ndotl = saturate(dot(normal, l.dir));
		
        float3 l0 = currentLight.lightsPos.xyz - position;
        float3 l1 = currentLight.lightsPosEnd.xyz - position;
        float lengthL0 = length(l0);
        float lengthL1 = length(l1);
        float NdotL0 = dot(normal, l0) / (2. * lengthL0);
        float NdotL1 = dot(normal, l1) / (2. * lengthL1);
        l.ndotl = (2. * saturate(NdotL0 + NdotL1)) /
			(lengthL0 * lengthL1 + dot(l0, l1) + 2.);
		
		// do a segment segment distance and ...
		// https://zalo.github.io/blog/closest-point-between-segments/
        l.reflDir = ClosestPointOnLineToSegment(position, position + reflect(viewDir, normal) * 1000, currentLight.lightsPos.xyz, currentLight.lightsPosEnd.xyz) - position;
        l.reflDist = length(l.reflDir);
        l.reflDir = l.reflDir / l.reflDist;

        l.color *= 1 / (l.dist * l.dist + 0.01);
    }
    return l;
}

// ------ Reprojection ------------------------------

static const
float2 pattern[9] =
{
    float2(0, 0),
	float2(-1, 0),
	float2(0, -1),
	float2(0, 1),
	float2(1, 0),
	float2(1, 1),
	float2(-1, 1),
	float2(1, -1),
	float2(-1, -1)
};

float2 ComputeMoVec(in float4 posCurr, in float4 posPrev)
{
    float4 moVec = float4((posCurr.xyz / posCurr.w) - (posPrev.xyz / posPrev.w), 0);
    //float4 moVec = float4((posCurr.xyz) - (posPrev.xyz), 0);
    //moVec.y = -moVec.y;
    moVec.xyz *= 0.5; //because we need to go from -1,1 to 0,1
    return moVec.xy;
}


// return confidence 0 to 1
float GetReprojectedUV(in int2 pixelPos, out float2 prevUV, in float2 resolution, float2 moVecDir = 1)
{
    float2 UV = (float2(pixelPos) + 0.5f) / resolution.xy;
    uint2 fullResPixelPos = UV * float2(globals.screenResolution.xy);
    float2 moVec = srv2Dfloat2[globals.moVec][fullResPixelPos].xy;
    moVec.y = -moVec.y;
    float2 reprojUV = UV - moVec;

    float blend = 0.00333;

    if (any(reprojUV.xy >= 1.0) || any(reprojUV.xy <= 0.0))
        blend = 1;
	
    reprojUV = saturate(reprojUV);
    float D = srv2Dfloat[globals.depth][fullResPixelPos];
    D = linearZ(D, camera.camClips.x, camera.camClips.y);
    float DPrev = srv2Dfloat[globals.previousDepth][reprojUV * globals.screenResolution];
    DPrev = linearZ(DPrev, camera.camClips.x, camera.camClips.y);
    if (abs(D - DPrev) > 0.0001)
        blend = 1;
	
    blend = saturate(blend);
	
    prevUV = reprojUV;
	
    return blend;
}

float GetReprojectedPixelPos(in int2 pixelPos, out int2 prevPixelPos, in uint2 resolution, float2 moVecDir = 1)
{
    float2 reprojUV;
    float blend = GetReprojectedUV(pixelPos, reprojUV, resolution, moVecDir);
	
    prevPixelPos = reprojUV * resolution.xy;
	
    return blend;
}
*/
// ------ Panini-------------------------------------

// Back-ported & adapted from the work of the Stockholm demo team - thanks Lasse
float2 Panini_UnitDistance(float2 view_pos)
{
	// Given
	//    S----------- E--X-------
	//    |      ` .  /,�
	//    |-- ---    Q
	//  1 |       ,�/  `
	//    |     ,� /    �
	//    |   ,�  /      `
	//    | ,�   /       .
	//    O`    /        .
	//    |    /         `
	//    |   /         �
	//  1 |  /         �
	//    | /        �
	//    |/_  .  �
	//    P
	//
	// Have E
	// Want to find X
	//
	// First apply tangent-secant theorem to find Q
	//   PE*QE = SE*SE
	//   QE = PE-PQ
	//   PQ = PE-(SE*SE)/PE
	//   Q = E*(PQ/PE)
	// Then project Q to find X

    const float d = 1.0;
    const float view_dist = 2.0;
    const float view_dist_sq = 4.0;

    float view_hyp = sqrt(view_pos.x * view_pos.x + view_dist_sq);

    float cyl_hyp = view_hyp - (view_pos.x * view_pos.x) / view_hyp;
    float cyl_hyp_frac = cyl_hyp / view_hyp;
    float cyl_dist = view_dist * cyl_hyp_frac;

    float2 cyl_pos = view_pos * cyl_hyp_frac;
    return cyl_pos / (cyl_dist - d);
}

float2 Panini_Generic(float2 view_pos, float d)
{
	// Given
	//    S----------- E--X-------
	//    |    `  ~.  /,�
	//    |-- ---    Q
	//    |        ,/    `
	//  1 |      ,�/       `
	//    |    ,� /         �
	//    |  ,�  /           �
	//    |,`   /             ,
	//    O    /
	//    |   /               ,
	//  d |  /
	//    | /                ,
	//    |/                .
	//    P 
	//    |              �
	//    |         , �
	//    +-    �
	//
	// Have E
	// Want to find X
	//
	// First compute line-circle intersection to find Q
	// Then project Q to find X

    float view_dist = 1.0 + d;
    float view_hyp_sq = view_pos.x * view_pos.x + view_dist * view_dist;

    float isect_D = view_pos.x * d;
    float isect_discrim = view_hyp_sq - isect_D * isect_D;

    float cyl_dist_minus_d = (-isect_D * view_pos.x + view_dist * sqrt(isect_discrim)) / view_hyp_sq;
    float cyl_dist = cyl_dist_minus_d + d;

    float2 cyl_pos = view_pos * (cyl_dist / view_dist);
    return cyl_pos / (cyl_dist - d);
}

inline uint3 ModulusI(uint3 a, uint3 b)
{
    return (uint3(a % b) + b) % b;
}

float3 StoreR11G11B10Normal(float3 normal)
{
    return normal * 0.5 + 0.5;
}
float3 ReadR11G11B10Normal(float3 normal)
{
    return normal * 2 - 1;
}
struct GBufferCameraData
{
    HLSL::Camera camera;
    float3 worldPos;
    float3 worldNorm;
    float3 viewDir;
    float viewDist;
};
GBufferCameraData GetGBufferCameraData(uint2 pixel)
{
    GBufferCameraData cd;
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    cd.camera = cameras[0]; //cullingContext.cameraIndex];
    
    Texture2D<float> depth = ResourceDescriptorHeap[cullingContext.depthIndex];
    Texture2D<float3> normalT = ResourceDescriptorHeap[cullingContext.normalIndex];
    cd.worldNorm = ReadR11G11B10Normal(normalT[pixel]);
    
    // inverse y depth depth[uint2(launchIndex.x, rtParameters.resolution.y - launchIndex.y)]
    float3 clipSpace = float3(pixel * cullingContext.resolution.zw * 2 - 1, depth[pixel]);
    float4 worldSpace = mul(cd.camera.viewProj_inv, float4(clipSpace.x, -clipSpace.y, clipSpace.z, 1));
    worldSpace.xyz /= worldSpace.w;
    
    float3 rayDir = worldSpace.xyz - cd.camera.worldPos.xyz;
    float rayLength = length(rayDir);
    rayDir /= rayLength;
    //worldSpace.xyz = cd.camera.worldPos.xyz + rayDir * rayLength;
    
    cd.viewDir = rayDir;
    cd.viewDist = rayLength;
    cd.worldPos = worldSpace.xyz;
    
    return cd;
}


struct SurfaceData
{
    float3 albedo;
    float metalness;
    float3 normal;
    float roughness;
};
SurfaceData GetSurfaceData()
{
    SurfaceData s;
    return s;
}

float3 BRDF(SurfaceData s, float3 viewDir, float3 lightDir, float3 lightColor)
{
    float NdotV = dot(-viewDir, s.normal);
    s.roughness = lerp(0, s.roughness, saturate(NdotV));
    float smooth = 1 - s.roughness;
    
    float NdotL = dot(s.normal, -lightDir);
    float3 diffuse = s.albedo * saturate(NdotL) * lightColor;
    float3 reflectViewDir = reflect(-viewDir, s.normal);
    float RdotL = dot(reflectViewDir, lightDir);
    float3 specular = pow(saturate(RdotL), 1 + smooth * 100) * lerp(lightColor, length(lightColor) * s.albedo, s.metalness) * smooth;

    return saturate(diffuse + specular);
}

SurfaceData GetRTSurfaceData(HLSL::Attributes attrib)
{
    StructuredBuffer<HLSL:: Instance > instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[InstanceID()];
    
    StructuredBuffer<HLSL:: Material > materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instance.materialIndex];
    
    StructuredBuffer<HLSL:: Mesh > meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    StructuredBuffer<HLSL:: Vertex > verticesData = ResourceDescriptorHeap[commonResourcesIndices.verticesHeapIndex];
    
    StructuredBuffer<uint> indicesData = ResourceDescriptorHeap[commonResourcesIndices.indicesHeapIndex];

    uint iBase = PrimitiveIndex() * 3 + mesh.indexOffset;
    uint i1 = indicesData[iBase + 0];
    uint i2 = indicesData[iBase + 1];
    uint i3 = indicesData[iBase + 2];

    float2 uv1 = verticesData[i1].uv;
    float2 uv2 = verticesData[i2].uv;
    float2 uv3 = verticesData[i3].uv;

    float2 uv = ((1 - attrib.bary.x - attrib.bary.y) * uv1 + attrib.bary.x * uv2 + attrib.bary.y * uv3);

    SurfaceData s;
    if (material.textures[0] != ~0)
    {
        Texture2D<float4> albedo = ResourceDescriptorHeap[material.textures[0]];
        s.albedo = albedo.SampleLevel(samplerLinear, uv, 0).xyz;
    }
    else
    {
        s.albedo = 0.66;
    }
    if (material.textures[2] != ~0)
    {
        Texture2D<float4> roughness = ResourceDescriptorHeap[material.textures[2]];
        s.roughness = roughness.SampleLevel(samplerLinear, uv, 0).x;
    }
    else
    {
        s.roughness = 0.99;
    }
    s.metalness = 0.1;

    float3 nrm1 = verticesData[i1].normal;
    float3 nrm2 = verticesData[i2].normal;
    float3 nrm3 = verticesData[i3].normal;

    float3 normal = ((1 - attrib.bary.x - attrib.bary.y) * nrm1 + attrib.bary.x * nrm2 + attrib.bary.y * nrm3);
    normal = normalize(normal);
    float4x4 worldMatrix = instance.unpack();
    float3 worldNormal = mul((float3x3) worldMatrix, normal);
    s.normal = normal;
    
    return s;
}

void UpdateGIReservoir(inout HLSL::GIReservoir previous, HLSL::GIReservoir current)
{
    previous.pos_Wcount.w += 1;
    previous.dir_Wsum.w += current.color_W.w;
    if(previous.color_W.w < current.color_W.w)
    {
        previous.color_W = current.color_W; // keep the new W so take the xyzw
        previous.dir_Wsum.xyz = current.dir_Wsum.xyz;
        previous.pos_Wcount.xyz = current.pos_Wcount.xyz;
    }
}

// Utility function to get a vector perpendicular to an input vector 
//    (from "Efficient Construction of Perpendicular Vectors Without Branching")
float3 getPerpendicularVector(float3 u)
{
    float3 a = abs(u);
    uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
    uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
    uint zm = 1 ^ (xm | ym);
    return cross(u, float3(xm, ym, zm));
}

float3 getCosHemisphereSample(inout uint randSeed, float3 hitNorm)
{
    hitNorm = normalize(hitNorm);
    // Get 2 random numbers to select our sample with
    float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

    // Cosine weighted hemisphere sample from RNG
    float3 bitangent = getPerpendicularVector(hitNorm);
    float3 tangent = cross(bitangent, hitNorm);
    float r = sqrt(randVal.x);
    float phi = 2.0f * 3.14159265f * randVal.y;

    // Get our cosine-weighted hemisphere lobe sample direction
    return normalize(tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x));
}
