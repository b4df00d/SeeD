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
uint xxhash(in uint p)
{
    const uint PRIME32_2 = 2246822519U, PRIME32_3 = 3266489917U;
    const uint PRIME32_4 = 668265263U,  PRIME32_5 = 374761393U;

    uint h32 = p + PRIME32_5;
    h32 = PRIME32_4 * ((h32 << 17) | (h32 >> (32 - 17)));
    h32 = PRIME32_2 * (h32 ^ (h32 >> 15));
    h32 = PRIME32_3 * (h32 ^ (h32 >> 13));

    return h32 ^ (h32 >> 16);
}

uint xxhash(in uint2 pixel)
{
    uint p = viewContext.frameTime << 20 | pixel.x << 10 | pixel.y;
    //p = 0 << 20 | pixel.x << 10 | pixel.y;
    return xxhash(p);
}
// Generates a seed for a random number generator from 2 inputs plus a backoff
uint initRand(uint2 pixel)
{
    return xxhash(pixel);
    //pixel *= 38742.6612f;
    //return initRand(pixel.x + (uint(viewContext.frameTime) % 123) * 2.621f, pixel.y + (uint(viewContext.frameTime) % 431) * 9.432f, 3);
    //return initRand(pixel.x + viewContext.frameTime, pixel.y + viewContext.frameTime, 3);
    //return initRand(pixel.x, pixel.y, 3);
}

// Takes our seed, updates it, and returns a pseudorandom float in [0..1]
float nextRand(inout uint s)
{
    s = (1664525u * s + 1013904223u);
    //s = xxhash(s);
    return float(s & 0x00FFFFFF) / float(0x01000000);
}


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
    float fBoundSphereDepth = saturate(clipSphere.z / clipSphere.w + (HLSL::reverseZ ? 0.00005 : - 0.001));
    
    float3 vHZB = float3(viewContext.renderResolution.x, viewContext.renderResolution.y, viewContext.HZBMipCount);
    Texture2D<float> tHZB = ResourceDescriptorHeap[viewContext.HZB];
    float4 vLBRT = ScreenSpaceBB(camera, viewSphere);
    
    float4 vToUV = float4(0.5f, -0.5f, 0.5f, -0.5f);
    float4 vUV = saturate(vLBRT.xwzy * vToUV + 0.5f);
    float4 vAABB = vUV * vHZB.xyxy;
    float2 vExtents = vAABB.zw - vAABB.xy;
    
    float fMipLevel = ceil(log2(max(vExtents.x, vExtents.y)));
    fMipLevel = clamp(fMipLevel, 0.0f, vHZB.z - 1.0f);
    
    float4 vOcclusionDepth = float4(tHZB.SampleLevel(samplerLinearClamp, vUV.xy, fMipLevel),
                                    tHZB.SampleLevel(samplerLinearClamp, vUV.zy, fMipLevel),
                                    tHZB.SampleLevel(samplerLinearClamp, vUV.zw, fMipLevel),
                                    tHZB.SampleLevel(samplerLinearClamp, vUV.xw, fMipLevel));
    
    float fMaxOcclusionDepth = max(max(max(vOcclusionDepth.x, vOcclusionDepth.y), vOcclusionDepth.z), vOcclusionDepth.w);
    bool bCulled = fMaxOcclusionDepth < fBoundSphereDepth;
    
    if(HLSL::reverseZ)
    {
        float fMinOcclusionDepth = min(min(min(vOcclusionDepth.x, vOcclusionDepth.y), vOcclusionDepth.z), vOcclusionDepth.w);
        bCulled = fMinOcclusionDepth > fBoundSphereDepth;
    }
    
    return bCulled;
}


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


float2 CalcVelocity(float4 newPos, float4 oldPos, float2 viewSize)
{
    oldPos /= oldPos.w;
    oldPos.xy = (oldPos.xy+1)/2.0f;
    oldPos.y = 1 - oldPos.y;
    
    newPos /= newPos.w;
    newPos.xy = (newPos.xy+1)/2.0f;
    newPos.y = 1 - newPos.y;
    
    return (oldPos - newPos).xy;
}

//  !! rip off from microsoft miniengine !!
// https://github.com/GPUOpen-LibrariesAndSDKs/nBodyD3D12/blob/master/MiniEngine/Core/Shaders/PixelPacking.hlsli

// RGBE, aka R9G9B9E5_SHAREDEXP, is an unsigned float HDR pixel format where red, green,
// and blue all share the same exponent.  The color channels store a 9-bit value ranging
// from [0/512, 511/512] which multiplies by 2^Exp and Exp ranges from [-15, 16].
// Floating point specials are not encoded.
uint PackRGBE(float3 rgb)
{
    // To determine the shared exponent, we must clamp the channels to an expressible range
    const float kMaxVal = asfloat(0x477F8000); // 1.FF x 2^+15
    const float kMinVal = asfloat(0x37800000); // 1.00 x 2^-16

    // Non-negative and <= kMaxVal
    rgb = clamp(rgb, 0, kMaxVal);

    // From the maximum channel we will determine the exponent.  We clamp to a min value
    // so that the exponent is within the valid 5-bit range.
    float MaxChannel = max(max(kMinVal, rgb.r), max(rgb.g, rgb.b));

    // 'Bias' has to have the biggest exponent plus 15 (and nothing in the mantissa).  When
    // added to the three channels, it shifts the explicit '1' and the 8 most significant
    // mantissa bits into the low 9 bits.  IEEE rules of float addition will round rather
    // than truncate the discarded bits.  Channels with smaller natural exponents will be
    // shifted further to the right (discarding more bits).
    float Bias = asfloat((asuint(MaxChannel) + 0x07804000) & 0x7F800000);

    // Shift bits into the right places
    uint3 RGB = asuint(rgb + Bias);
    uint E = (asuint(Bias) << 4) + 0x10000000;
    return E | RGB.b << 18 | RGB.g << 9 | (RGB.r & 0x1FF);
}

float3 UnpackRGBE(uint p)
{
    float3 rgb = uint3(p, p >> 9, p >> 18) & 0x1FF;
    return ldexp(rgb, (int)(p >> 27) - 24);
}

// This non-standard variant applies a non-linear ramp to the mantissa to get better precision
// with bright and saturated colors.  These colors tend to have one or two channels that prop
// up the shared exponent, leaving little to no information in the dark channels.
uint PackRGBE_sqrt(float3 rgb)
{
    // To determine the shared exponent, we must clamp the channels to an expressible range
    const float kMaxVal = asfloat(0x477FFFFF); // 1.FFFFFF x 2^+15
    const float kMinVal = asfloat(0x37800000); // 1.000000 x 2^-16

    rgb = clamp(rgb, 0, kMaxVal);

    float MaxChannel = max(max(kMinVal, rgb.r), max(rgb.g, rgb.b));

    // Scaling the maximum channel puts it into the range [0, 1).  It does this by negating
    // and subtracting one from the max exponent.
    float Scale = asfloat((0x7EFFFFFF - asuint(MaxChannel)) & 0x7F800000);
    uint3 RGB = sqrt(rgb * Scale) * 511.0 + 0.5;
    uint E = (0x47000000 - asuint(Scale)) << 4;
    return E | RGB.b << 18 | RGB.g << 9 | RGB.r;
}

float3 UnpackRGBE_sqrt(uint p)
{
    float3 rgb = (uint3(p, p >> 9, p >> 18) & 0x1FF) / 511.0;
    return ldexp(rgb * rgb, (int)(p >> 27) - 15);
}

// The standard 32-bit HDR color format
uint Pack_R11G11B10_FLOAT( float3 rgb )
{
	uint r = (f32tof16(rgb.x) << 17) & 0xFFE00000;
	uint g = (f32tof16(rgb.y) << 6 ) & 0x001FFC00;
	uint b = (f32tof16(rgb.z) >> 5 ) & 0x000003FF;
	return r | g | b;
}

float3 Unpack_R11G11B10_FLOAT( uint rgb )
{
	float r = f16tof32((rgb >> 17) & 0x7FF0);
	float g = f16tof32((rgb >> 6 ) & 0x7FF0);
	float b = f16tof32((rgb << 5 ) & 0x7FE0);
	return float3(r, g, b);
}

// end microsoft miniengine rip off

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
    float3 offsetedWorldPos;
    float3 worldNorm;
    float3 viewDir;
    float viewDist;
    float viewDistDiff;
    uint2 pixel;
    uint2 previousPixel;
    float2 previousUV;
    float previousViewDist;
};
GBufferCameraData GetGBufferCameraData(uint2 pixel)
{
    GBufferCameraData cd;
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    cd.camera = cameras[0]; //viewContext.cameraIndex];
    
    Texture2D<float3> normalT = ResourceDescriptorHeap[viewContext.normalIndex];
    cd.worldNorm = ReadR11G11B10Normal(normalT[pixel]);
    
    // inverse y depth depth[uint2(launchIndex.x, rtParameters.resolution.y - launchIndex.y)]
    Texture2D<float> depth = ResourceDescriptorHeap[viewContext.depthIndex];
    float3 clipSpace = float3(pixel * viewContext.renderResolution.zw * 2 - 1, depth[pixel]);
    float4 worldSpace = mul(cd.camera.viewProj_inv, float4(clipSpace.x, -clipSpace.y, clipSpace.z, 1));
    worldSpace.xyz /= worldSpace.w;
    
    float3 rayDir = worldSpace.xyz - cd.camera.worldPos.xyz;
    float rayLength = length(rayDir);
    rayDir /= rayLength;
    
    Texture2D<float2> motionT = ResourceDescriptorHeap[viewContext.motionIndex];
    float2 motion = motionT[pixel.xy];
    //cd.previousPixel = min(max(1, pixel.xy + int2(motion * (viewContext.renderResolution.xy))), (viewContext.renderResolution.xy-1));
    float2 previousPixel = ((pixel.xy+0.5)*viewContext.renderResolution.zw) + motion;
    //previousPixel = min(max(1, previousPixel), (viewContext.renderResolution.xy-1));
    cd.previousUV = saturate(previousPixel);
    cd.previousPixel = cd.previousUV * viewContext.renderResolution.xy;
    
    Texture2D<float> previousDepth = ResourceDescriptorHeap[viewContext.HZB];
    float3 previousClipSpace = float3(cd.previousUV * 2 - 1, previousDepth[cd.previousPixel]);
    float4 previousWorldSpace = mul(cd.camera.previousViewProj_inv, float4(previousClipSpace.x, -previousClipSpace.y, previousClipSpace.z, 1));
    previousWorldSpace.xyz /= previousWorldSpace.w;
    
    float3 previousRayDir = previousWorldSpace.xyz - cd.camera.previousWorldPos.xyz;
    float previousRayLength = length(previousRayDir);
    previousRayDir /= previousRayLength;
    
    cd.viewDir = rayDir;
    cd.viewDist = rayLength;
    cd.worldPos = worldSpace.xyz;
    cd.viewDistDiff = abs(rayLength - previousRayLength);
    cd.previousViewDist = previousRayLength;
    
    cd.offsetedWorldPos = cd.worldPos - (cd.viewDir * cd.viewDist * 0.005) + (cd.worldNorm * cd.viewDist * 0.002);
    
    cd.pixel = pixel;
    
    return cd;
}

struct SurfaceData
{
    float4 albedo; //with alpha
    float3 normal;
    float3 tangent;
    float3 binormal;
    float metalness;
    float roughness;
    float specularTint;
    float _specular;
    float sheen;
    float sheenTint;
    float anisotropic;
    float clearcoat;
    float clearcoatGloss;
    float subsurface;
};
SurfaceData GetSurfaceData(HLSL::Material material, float2 uv, float3 normal, float3 tangent, float3 binormal)
{
    SurfaceData s;
    uint textureIndex = ~0;
    
    s.albedo = material.parameters[0];
    textureIndex = material.textures[0];
    if(textureIndex != ~0)
    {
        Texture2D<float4> albedo = ResourceDescriptorHeap[textureIndex];
        #ifdef RAY_DISPATCH
        s.albedo *= albedo.SampleLevel(samplerLinear, uv, 3);
        #else
        s.albedo *= albedo.Sample(samplerLinear, uv);
        #endif
        s.albedo.xyz = pow(s.albedo.xyz, 1.f/2.2f);
        if(length(s.albedo.xyz) < 0.001) s.albedo.xyz = float3(1,0,1);
    }
    
    s.roughness = material.parameters[1];
    textureIndex = material.textures[1];
    if(textureIndex != ~0)
    {
        Texture2D<float4> roughtness = ResourceDescriptorHeap[textureIndex];
        #ifdef RAY_DISPATCH
        s.roughness *= roughtness.SampleLevel(samplerLinear, uv, 3).x;
        #else
        s.roughness *= roughtness.Sample(samplerLinear, uv).x;
        #endif
    }
    
    s.metalness = material.parameters[2];
    textureIndex = material.textures[2];
    if(textureIndex != ~0)
    {
        Texture2D<float4> metalness = ResourceDescriptorHeap[textureIndex];
        #ifdef RAY_DISPATCH
        s.metalness = metalness.SampleLevel(samplerLinear, uv, 3).x;
        #else
        s.metalness = metalness.Sample(samplerLinear, uv).x;
        #endif
    }
    
    s.normal = normal;
    s.tangent = tangent;
    s.binormal = binormal;
    textureIndex = material.textures[3];
    if(textureIndex != ~0)
    {
        Texture2D<float4> normals = ResourceDescriptorHeap[textureIndex];
        #ifdef RAY_DISPATCH
        s.normal *= normals.SampleLevel(samplerLinear, uv, 3).xyz;
        #else
        s.normal *= normals.Sample(samplerLinear, uv).xyz;
        #endif
    }
    
    s.specularTint = 1;
    s._specular = 1;
    s.sheen = 0;
    s.sheenTint = 0.5;
    s.anisotropic = 0;
    s.clearcoat = 0;
    s.clearcoatGloss = 1;
    s.subsurface = 0;
    
    return s;
}

SurfaceData GetSurfaceData(uint2 pixel)
{
    Texture2D<float4> albedo = ResourceDescriptorHeap[viewContext.albedoIndex];
    Texture2D<float> metalness = ResourceDescriptorHeap[viewContext.metalnessIndex];
    Texture2D<float> roughness = ResourceDescriptorHeap[viewContext.roughnessIndex];
    Texture2D<float3> normal = ResourceDescriptorHeap[viewContext.normalIndex];
    
    SurfaceData s;
    
    s.albedo = albedo[pixel];
    s.normal = ReadR11G11B10Normal(normal[pixel]);
    s.metalness = metalness[pixel];
    s.roughness = roughness[pixel];
    s.tangent = cross(s.normal, float3(1, 0, 0));
    s.binormal = cross(s.normal, s.tangent);
    s.specularTint = 0;
    s._specular = 0;
    s.sheen = 0;
    s.sheenTint = 0.5;
    s.anisotropic = 0;
    s.clearcoat = 0;
    s.clearcoatGloss = 1;
    s.subsurface = 0;
    
    return s;
}

// disney stuff https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
// explanation for cpp https://schuttejoe.github.io/post/disneybsdf/
// raytraced https://www.shadertoy.com/view/cll3R4
//https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// simple stuff http://filmicworlds.com/blog/optimizing-ggx-shaders-with-dotlh/


// copy past from https://discussions.unity.com/t/disney-principled-brdf-shader/742743
static const float PI = 3.14159265358979323846;

float sqr(float x) { return x*x; }

float SchlickFresnel(float u)
{
    float m = saturate(1.0-u);
    float m2 = m*m;
    return m2*m2*m; // pow(m,5)
}

float GTR1(float NdotH, float a)
{
    if (a >= 1) return 1/PI;
    float a2 = a*a;
    float t = 1 + (a2-1)*NdotH*NdotH;
    return (a2-1) / (PI*log(a2)*t);
}

float GTR2(float NdotH, float a)
{
    float a2 = a*a;
    float t = 1 + (a2-1)*NdotH*NdotH;
    return a2 / (PI * t*t);
}

float GTR2_aniso(float NdotH, float HdotX, float HdotY, float ax, float ay)
{
    return 1 / (PI * ax*ay * sqr( sqr(HdotX/ax) + sqr(HdotY/ay) + NdotH*NdotH ));
}

float smithG_GGX(float NdotV, float alphaG)
{
    float a = alphaG*alphaG;
    float b = NdotV*NdotV;
    return 1 / (NdotV + sqrt(a + b - a*b));
}

float smithG_GGX_aniso(float NdotV, float VdotX, float VdotY, float ax, float ay)
{
    return 1 / (NdotV + sqrt( sqr(VdotX*ax) + sqr(VdotY*ay) + sqr(NdotV) ));
}

float3 mon2lin(float3 x)
{
    return float3(pow(x[0], 2.2), pow(x[1], 2.2), pow(x[2], 2.2));
}

float3 BRDF(SurfaceData s, float3 V, float3 L, float3 LColor)
{
    s.normal = normalize(s.normal);
    L = normalize(-L);
    V = normalize(-V);
    
    
    float NdotL = max(dot(s.normal,L),0.0);
    float NdotV = max(dot(s.normal,V),0.0);
    

    float3 H = normalize(L+V);
    float NdotH = max(dot(s.normal,H),0.0);
    float LdotH = max(dot(L,H),0.0);

    float3 Cdlin = mon2lin(s.albedo.xyz);
    float Cdlum = .3*Cdlin[0] + .6*Cdlin[1]  + .1*Cdlin[2]; // luminance approx.

    float3 Ctint = Cdlum > 0 ? Cdlin/Cdlum : float3(1,1,1); // normalize lum. to isolate hue+sat
    float3 Cspec0 = lerp(s._specular*.08*lerp(float3(1,1,1), Ctint, s.specularTint), Cdlin, s.metalness);
    float3 Csheen = lerp(float3(1,1,1), Ctint, s.sheenTint);

    // Diffuse fresnel - go from 1 at normal incidence to .5 at grazing
    // and lerp in diffuse retro-reflection based on roughness
    float FL = SchlickFresnel(NdotL);
    float FV = SchlickFresnel(NdotV);
    float Fd90 = 0.5 + 2 * LdotH*LdotH*s.roughness;
    float Fd = lerp(1.0, Fd90, FL) * lerp(1.0, Fd90, FV);

    // Based on Hanrahan-Krueger brdf approximation of isotropic bssrdf
    // 1.25 scale is used to (roughly) preserve albedo
    // Fss90 used to "flatten" retroreflection based on roughness
    float Fss90 = LdotH*LdotH*s.roughness;
    float Fss = lerp(1.0, Fss90, FL) * lerp(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1 / (NdotL + NdotV) - .5) + .5);

    // specular
    float aspect = sqrt(1-s.anisotropic*.9);
    aspect = 1;
    float ax = max(.001, sqr(s.roughness)/aspect);
    float ay = max(.001, sqr(s.roughness)*aspect);
    //float Ds = GTR2_aniso(NdotH, dot(H, s.tangent), dot(H, s.binormal), ax, ay);
    float Ds = GTR2(NdotH, ax);
    float FH = SchlickFresnel(LdotH);
    float3 Fs = lerp(Cspec0, float3(1,1,1), FH);
    //float Gs  = smithG_GGX_aniso(NdotL, dot(L, s.tangent), dot(L, s.binormal), ax, ay);
    //Gs *= smithG_GGX_aniso(NdotV, dot(V, s.tangent), dot(V, s.binormal), ax, ay);
    float Gs  = smithG_GGX(NdotL, ax);
    Gs *= smithG_GGX(NdotV, ax);

    // sheen
    float3 Fsheen = FH * s.sheen * Csheen;

    // clearcoat (ior = 1.5 -> F0 = 0.04)
    float Dr = GTR1(NdotH, lerp(.1,.001,s.clearcoatGloss));
    float Fr = lerp(.04, 1.0, FH);
    float Gr = smithG_GGX(NdotL, .25) * smithG_GGX(NdotV, .25);

    float3 result = ((1/PI) * lerp(Fd, ss, s.subsurface)*Cdlin + Fsheen) * (1-s.metalness) + Gs*Fs*Ds + .25*s.clearcoat*Gr*Fr*Dr;
    result *= LColor;
    result *= NdotL;
    return result;
}

float3 ComputeLight(HLSL::Light light, float shadow, SurfaceData s, float3 V)
{
    float3 brdf = BRDF(s, V, light.dir.xyz, light.color.xyz);
    float NdotL = max(dot(s.normal,-light.dir.xyz),0.0);
    float3 lighted = brdf * NdotL * shadow;
    return lighted;
}

float3 Sky(float3 direction)
{
    float dotUp = saturate(pow(saturate(dot(direction, float3(0, 1, 0))), 0.5));
    float dotDown = saturate(pow(saturate(dot(direction, float3(0, -1, 0)) * 2), 1));
    float3 sky = normalize(lerp(float3(1, 0.66, 0.66), float3(0.33, 0.5, 1), dotUp));
    sky = lerp(sky, float3(0, 0, 0), dotDown);
    return sky * 3;
}

SurfaceData GetRTSurfaceData(
uint committedInstanceIndex,
uint committedPrimitiveIndex,
uint committedGeometryIndex,
float2 bary)
{
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[committedInstanceIndex];
    
    StructuredBuffer<HLSL::Material> materials = ResourceDescriptorHeap[commonResourcesIndices.materialsHeapIndex];
    HLSL::Material material = materials[instance.materialIndex];
    
    StructuredBuffer<HLSL::Mesh> meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
    HLSL::Mesh mesh = meshes[instance.meshIndex];
    
    StructuredBuffer<HLSL::Vertex> verticesData = ResourceDescriptorHeap[commonResourcesIndices.verticesHeapIndex];
    
    StructuredBuffer<uint> indicesData = ResourceDescriptorHeap[commonResourcesIndices.indicesHeapIndex];
    
    uint iBase = committedPrimitiveIndex * 3 + mesh.indexOffset;
    uint i1 = indicesData[iBase + 0];
    uint i2 = indicesData[iBase + 1];
    uint i3 = indicesData[iBase + 2];

    float2 uv1 = verticesData[i1].uv;
    float2 uv2 = verticesData[i2].uv;
    float2 uv3 = verticesData[i3].uv;

    float2 uv = ((1 - bary.x - bary.y) * uv1 + bary.x * uv2 + bary.y * uv3);
    
    float3 nrm1 = verticesData[i1].normal;
    float3 nrm2 = verticesData[i2].normal;
    float3 nrm3 = verticesData[i3].normal;

    float3 normal = ((1 - bary.x - bary.y) * nrm1 + bary.x * nrm2 + bary.y * nrm3);
    normal = normalize(normal);
    float4x4 worldMatrix = instance.unpack(instance.current);
    float3 worldNormal = mul((float3x3) worldMatrix, normal);
    worldNormal = normalize(worldNormal);

    SurfaceData s = GetSurfaceData(material, uv, worldNormal, 0, 0);
    
    return s;
}

static uint maxFrameFilteringCount = 6;
struct RESTIRRay
{
    float3 Origin;
    float3 Direction;
    float3 HitPosition;
    float3 HitRadiance;
    float3 HitNormal;
};

HLSL::GIReservoirCompressed PackGIReservoir(HLSL::GIReservoir r)
{
    HLSL::GIReservoirCompressed result;
    //result.color = PackRGBE_sqrt(r.color_W.xyz);
    result.color = r.color_W.xyz;
    result.Wcount_W = (asuint(f32tof16(r.dir_Wcount.w)) << 16) + asuint(f32tof16(r.color_W.w));
    //result.dir = Pack_R11G11B10_FLOAT(normalize(r.dir_Wcount.xyz) * 0.5 + 0.5);
    //result.dir = normalize(r.dir_Wcount.xyz) * 0.5 + 0.5;
    //result.dir = r.dir_Wcount.xyz;
    result.hit_Wsum = r.hit_Wsum;
    return result;
}

HLSL::GIReservoir UnpackGIReservoir(HLSL::GIReservoirCompressed r)
{
    HLSL::GIReservoir result;
    //result.color_W.xyz = UnpackRGBE_sqrt(r.color);
    result.color_W.xyz = r.color;
    result.color_W.w = f16tof32(r.Wcount_W & 0xffff);
    result.dir_Wcount.w = f16tof32(r.Wcount_W >> 16u);
    //result.dir_Wcount.xyz = normalize(Unpack_R11G11B10_FLOAT(r.dir) * 2 - 1);
    result.dir_Wcount.xyz = float3(0,1,0);
    //result.dir_Wcount.xyz = normalize(r.dir * 2 - 1);
    //result.dir_Wcount.xyz = r.dir;
    result.hit_Wsum = r.hit_Wsum;
    return result;
}

void UpdateGIReservoir(inout HLSL::GIReservoir previous, HLSL::GIReservoir current, float rand)
{
    if(rand < (current.color_W.w / (previous.color_W.w + current.color_W.w)) || rand == 1)
    //if(rand <= (current.color_W.w / ((previous.hit_Wsum.w / previous.dir_Wcount.w) + current.color_W.w)))
    {
        previous.color_W = current.color_W; // keep the new W so take the xyzw
        previous.hit_Wsum.xyz = current.hit_Wsum.xyz;
        previous.dir_Wcount.xyz = current.dir_Wcount.xyz;
    }
    previous.hit_Wsum.w += current.hit_Wsum.w;
    previous.dir_Wcount.w += current.dir_Wcount.w;
}

void ScaleGIReservoir(inout HLSL::GIReservoir r, uint frameFilteringCount)
{
    if (r.dir_Wcount.w >= frameFilteringCount)
    {
        float factor = max(0, float(frameFilteringCount) / max(r.dir_Wcount.w, 1.0f));
        //r.color_W.w *= factor;
        r.hit_Wsum.w *= factor;
        r.dir_Wcount.w *= factor;
    }
}

void RESTIR(RESTIRRay restirRay, uint previousReservoirIndex, uint currentReservoirIndex, in GBufferCameraData cd, uint seed)
{
    RWStructuredBuffer<HLSL::GIReservoirCompressed> previousgiReservoir = ResourceDescriptorHeap[previousReservoirIndex];
    HLSL::GIReservoir r = UnpackGIReservoir(previousgiReservoir[cd.previousPixel.x + cd.previousPixel.y * viewContext.renderResolution.x]);
    // if not first time fill with previous frame reservoir
    if (viewContext.frameNumber == 0)
    {
        r.color_W = 0;
        r.dir_Wcount = float4(0,0,0,1);
        r.hit_Wsum = 0;
    }
        
    float blend = 1-saturate(cd.viewDistDiff * pow(cd.viewDist, 0.5) * 0.15 - 0.1);
    uint frameFilteringCount = max(1, blend * maxFrameFilteringCount);
    
    HLSL::GIReservoir newR;
    float W = dot(restirRay.HitRadiance.xyz, float3(0.3, 0.59, 0.11));
    newR.color_W = float4(restirRay.HitRadiance.xyz, W);
    newR.dir_Wcount = float4(restirRay.Direction, 1); // there is no dir in dir_Wcount when packed !
    newR.hit_Wsum = float4(restirRay.HitPosition, W);
        
    UpdateGIReservoir(r, newR, nextRand(seed));
    ScaleGIReservoir(r, frameFilteringCount);
    
    RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[currentReservoirIndex];
    giReservoir[cd.pixel.x + cd.pixel.y * viewContext.renderResolution.x] = PackGIReservoir(r);
}

float3 RESTIRLight(uint currentReservoirIndex, in GBufferCameraData cd, in SurfaceData s)
{
    StructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[currentReservoirIndex];   
    HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[cd.pixel.x + cd.pixel.y * viewContext.renderResolution.x]);
    
    if(any(r.color_W.xyz > 0))
    {
        HLSL::Light light;
        light.pos = float4(r.hit_Wsum.xyz, 0);
        light.dir = float4(normalize(cd.worldPos - r.hit_Wsum.xyz), 0);
        light.color.xyz = r.color_W.xyz / r.color_W.w * (r.hit_Wsum.w / r.dir_Wcount.w);
        light.range = 1;
        light.angle = 1;
        return ComputeLight(light, 1, s, cd.viewDir);
    }
    return float3(0, 0, 0);
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

float3 getCosHemisphereSample(in uint randSeed, float3 hitNorm)
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
    float3 dir = normalize(tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x));
    
    //int3 dirInt = dir * 128;
    //dir = normalize(float3(dirInt));
    
    return dir;
}

float3 SampleProbes(HLSL::RTParameters rtParameters, float3 worldPos, SurfaceData s, bool writeWorldPosAsOffset = false)
{
    worldPos += s.normal * 0.1;
    uint probeGridIndex = 0;
    HLSL::ProbeGrid probes = rtParameters.probes[probeGridIndex];
    while(probeGridIndex < 3 && (any(worldPos.xyz < probes.probesBBMin.xyz) || any(worldPos.xyz > probes.probesBBMax.xyz)))
    {
        probeGridIndex++;
        probes = rtParameters.probes[probeGridIndex];
    }
    if(probeGridIndex >= 3) return 0;
    float3 cellSize = float3(probes.probesBBMax.xyz - probes.probesBBMin.xyz) / float3(probes.probesResolution.xyz);
    
    if(writeWorldPosAsOffset)
    {
        //non jittered sample pos for setting pos offset
        int3 launchIndex = (worldPos - probes.probesBBMin.xyz) / (probes.probesBBMax.xyz - probes.probesBBMin.xyz) * probes.probesResolution.xyz;
        uint3 wrapIndex = ModulusI(launchIndex.xyz + probes.probesAddressOffset.xyz, probes.probesResolution.xyz);
        uint probeIndex = wrapIndex.x + wrapIndex.y * probes.probesResolution.x + wrapIndex.z * (probes.probesResolution.x * probes.probesResolution.y);
        RWStructuredBuffer<HLSL::ProbeData> probesBuffer = ResourceDescriptorHeap[probes.probesIndex];
        float3 probeCenter = launchIndex * cellSize + probes.probesBBMin.xyz;
        float3 probeOffset = worldPos - probeCenter;
        float currentOffsetLength = length(probesBuffer[probeIndex].position.xyz);
        if(currentOffsetLength == 0 || length(probeOffset) < currentOffsetLength) probesBuffer[probeIndex].position = float4(probeOffset, 0);
    }
    
    //with jitter for sampling
    float3 jitteredPos = worldPos + s.normal * cellSize * 0.5;
    jitteredPos.x += sin(worldPos.z * 25) * cellSize.x * 0.125;
    jitteredPos.z += sin(worldPos.x * 25) * cellSize.z * 0.125;
    jitteredPos.y += sin(worldPos.y * 25) * cellSize.y * 0.125;
    int3 launchIndex = (jitteredPos - probes.probesBBMin.xyz) / (probes.probesBBMax.xyz - probes.probesBBMin.xyz) * probes.probesResolution.xyz;
    uint3 wrapIndex = ModulusI(launchIndex.xyz + probes.probesAddressOffset.xyz, probes.probesResolution.xyz);
    uint probeIndex = wrapIndex.x + wrapIndex.y * probes.probesResolution.x + wrapIndex.z * (probes.probesResolution.x * probes.probesResolution.y);
    StructuredBuffer<HLSL::ProbeData> probesBuffer = ResourceDescriptorHeap[probes.probesIndex];
    HLSL::ProbeData probe = probesBuffer[probeIndex];
    float3 result = max(0.0f, shUnproject(probe.sh.R, probe.sh.G, probe.sh.B, s.normal)); // A "max" is usually recomended to avoid negative values (can happen with SH)
    return result * 0.33; // TODO : why probe too bright ?!
}

void TraceRayCommon(HLSL::RTParameters rtParameters,
    uint RayFlags,
    uint InstanceInclusionMask,
    uint RayContributionToHitGroupIndex,
    uint MultiplierForGeometryContributionToHitGroupIndex,
    uint MissShaderIndex,
    RayDesc Ray,
    inout HLSL::HitInfo Payload);
struct MyCustomIntersectionAttributes { float4 a; float3 b; };
bool MyProceduralIntersectionEnumerator(float tHit, MyCustomIntersectionAttributes candidateAttribs, uint candidateInstanceIndex, uint candidatePrimitiveIndex, uint candidateGeometryIndex)
{
    return false;
}
bool MyProceduralAlphaTestLogic(float tHit, MyCustomIntersectionAttributes candidateAttribs, uint candidateInstanceIndex, uint candidatePrimitiveIndex, uint candidateGeometryIndex)
{
    return false;
}
bool MyAlphaTestLogic(uint candidateInstanceIndex, uint candidatePrimitiveIndex, uint candidateGeometryIndex, uint candidateTriangleRayT, float2 candidateTriangleBarycentrics, bool candidateTriangleFrontFace)
{
    return false;
}
bool MyLogicSaysStopSearchingForSomeReason()
{
    return false;
}

//return color + distance cosine weighted on surface normal
RESTIRRay DirectLight(HLSL::RTParameters rtParameters, SurfaceData s, RESTIRRay restirRay, uint depth, inout uint seed)
{
    StructuredBuffer<HLSL::Light> lights = ResourceDescriptorHeap[commonResourcesIndices.lightsHeapIndex];
    uint lightIndex = floor(nextRand(seed) * commonResourcesIndices.lightCount);
    //lightIndex += viewContext.frameNumber;
    lightIndex = lightIndex % commonResourcesIndices.lightCount;
    HLSL::Light light = lights[lightIndex];
    
    float3 lightDir = -light.dir.xyz;
    if (light.type == 0) // directional
    {
        light.color.xyz *= 1;
        lightDir = -light.dir.xyz;
        light.pos.xyz = lightDir * 10000.0;
    }
    else if (light.type == 1) // point
    {
        lightDir = light.pos.xyz - restirRay.Origin;
        float lightDist = length(lightDir);
        light.color.xyz /= pow(lightDist + 1, 2);
        lightDir /= lightDist;
    }
    else if (light.type == 2) // spot
    {
        lightDir = light.pos.xyz - restirRay.Origin;
        float lightDist = length(lightDir);
        light.color.xyz /= pow(lightDist + 1, 2);
        light.color.xyz *= saturate(dot(light.dir.xyz, lightDir) * 5.0 - light.angle);
        lightDir /= lightDist;
    }
    
    restirRay.Direction = lightDir;
    
    RayDesc ray;
    ray.Origin = restirRay.Origin;
    ray.Direction = restirRay.Direction;
    ray.TMin = 0.001;
    ray.TMax = 10000;
    
    HLSL::HitInfo newPayload;
    newPayload.color = float3(0.0, 0.0, 0.0);
    newPayload.hitPos = 0;
    newPayload.hitDistance = 0;
    newPayload.type = 0; //direct / visibility / shadow
    newPayload.depth = depth;
    newPayload.seed = seed;
    
    TraceRayCommon(rtParameters, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, newPayload);
    
    // did we hit something ? by knowing if the hit is before or after the light
    float visible = newPayload.hitDistance >= (length(light.pos.xyz - ray.Origin) * 0.999f) ? 1 : 0;
    float3 color = (light.color.xyz * visible);// * saturate(dot(s.normal, lightDir)) * s.albedo.xyz;
    
    restirRay.HitRadiance = color;
    restirRay.HitPosition = light.pos.xyz;
    
    return restirRay;
}

//return color + distance cosine weighted on surface normal
RESTIRRay IndirectLight(HLSL::RTParameters rtParameters, SurfaceData s, RESTIRRay restirRay, uint depth, inout uint seed)
{
    RayDesc ray;
    ray.Origin = restirRay.Origin;
    ray.Direction = restirRay.Direction;
    ray.TMin = 0.001;
    ray.TMax = 10000;
    
    HLSL::HitInfo newPayload;
    newPayload.color = float3(0.0, 0.0, 0.0);
    newPayload.hitPos = 0;
    newPayload.hitDistance = 0;
    newPayload.type = 1; //indirect
    newPayload.depth = depth;
    newPayload.seed = seed;
        
    TraceRayCommon(rtParameters, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, newPayload);
    
    float3 color = newPayload.color;
    
    if(depth > 0)
    {
        color = color * saturate(dot(s.normal, restirRay.Direction)) * s.albedo.xyz;
    }
        
    restirRay.HitNormal = newPayload.hitNorm;
    restirRay.HitPosition = newPayload.hitPos;
    restirRay.HitRadiance = color;
    
    return restirRay;
}

float3 PathTrace(HLSL::RTParameters rtParameters, SurfaceData s, float3 hitPos, uint depth, inout uint seed)
{
    RESTIRRay restirRay;
    restirRay.Origin = hitPos;
    
    if (depth < HLSL::maxRTDepth)
    {
        restirRay = DirectLight(rtParameters, s, restirRay, depth, seed);
    }
    
    #if 0
    if (depth + 1 < HLSL::maxRTDepth)
    {
        float3 bounceDir;
        float3 bounceNorm;
        float3 bounceHit;
        color.xyz += IndirectLightR(rtParameters, s, hitPos, depth, seed, bounceDir, bounceNorm, bounceHit).xyz;
    }
    #elif 1
    restirRay.HitRadiance += SampleProbes(rtParameters, hitPos, s, true).xyz;
    #else
    if(nextRand(seed) > 0.66)
    {
        if (depth + 1 < HLSL::maxRTDepth)
        {
            float3 bounceDir;
            float3 bounceNorm;
            float3 bounceHit;
            color.xyz += IndirectLightR(rtParameters, s, hitPos, depth, seed, bounceDir, bounceNorm, bounceHit).xyz;
        }
    }
    else
    {
        restirRay.hitRadiance += SampleProbes(rtParameters, hitPos, s, true).xyz;
    }
    #endif
    
    return restirRay.HitRadiance;
}

void CommonHit(HLSL::RTParameters rtParameters, 
uint committedInstanceIndex,
uint committedPrimitiveIndex,
uint committedGeometryIndex,
float2 committedTriangleBarycentrics,
float3 WorldRayOrigin,
float3 WorldRayDirection,
float RayTCurrent,
bool committedTriangleFrontFace,
inout HLSL::HitInfo payload)
{
    payload.hitDistance = RayTCurrent;
    if(payload.type == 0) // shadow ray hit
    {
        payload.color = 0;
        return;
    }
    
    SurfaceData s = GetRTSurfaceData(committedInstanceIndex, committedPrimitiveIndex, committedGeometryIndex, committedTriangleBarycentrics);
    if(dot(s.normal, WorldRayDirection) > 0) s.normal = -s.normal; // if we touch the backface, invert the normal ?
    float3 position = WorldRayOrigin + WorldRayDirection * (RayTCurrent * 0.9999f); 
    position = position + s.normal * (RayTCurrent * 0.0001f);
    payload.hitPos = position;
    payload.hitNorm = s.normal;
    payload.hitDistance = RayTCurrent;
   
    payload.color = PathTrace(rtParameters, s, payload.hitPos, payload.depth + 1, payload.seed);
}
void ShadeMyProceduralPrimitiveHit(MyCustomIntersectionAttributes committedCustomAttribs, uint committedInstanceIndex, uint committedPrimitiveIndex, uint committedGeometryIndex, float committedRayT)
{
    
}
void CommonMiss(float3 WorldRayOrigin, float3 WorldRayDirection, float RayTCurrent, inout HLSL::HitInfo Payload)
{
    Payload.hitDistance = RayTCurrent;
    Payload.hitPos = WorldRayOrigin + WorldRayDirection * RayTCurrent;
    Payload.color = Sky(WorldRayDirection);
}

void TraceRayCommon(HLSL::RTParameters rtParameters,
    uint RayFlags,
    uint InstanceInclusionMask,
    uint RayContributionToHitGroupIndex,
    uint MultiplierForGeometryContributionToHitGroupIndex,
    uint MissShaderIndex,
    RayDesc Ray,
    inout HLSL::HitInfo Payload)
{
    RaytracingAccelerationStructure AccelerationStructure = ResourceDescriptorHeap[rtParameters.BVH];
#ifdef RAY_DISPATCH
    TraceRay(AccelerationStructure, RayFlags, InstanceInclusionMask, RayContributionToHitGroupIndex, MultiplierForGeometryContributionToHitGroupIndex, MissShaderIndex, Ray, Payload);
#else
    // Instantiate ray query object.
    // Template parameter allows driver to generate a specialized
    // implemenation.  No specialization in this example.
    RayQuery<RAY_FLAG_NONE> q;

    // Set up a trace
    q.TraceRayInline(AccelerationStructure, q.RayFlags(), InstanceInclusionMask, Ray);

    // Storage for procedural primitive hit attributes
    MyCustomIntersectionAttributes committedCustomAttribs;

    // Proceed() is where behind-the-scenes traversal happens,
    // including the heaviest of any driver inlined code.
    // Returns TRUE if there's a task for the shader to perform
    // as part of traversal
    while(q.Proceed())
    {
        switch(q.CandidateType())
        {
            case CANDIDATE_PROCEDURAL_PRIMITIVE:
            {
                float tHit;
                MyCustomIntersectionAttributes candidateAttribs;

                // For procedural primitives, opacity is handled manually -
                // if an intersection is determined to not be opaque, just don't consider it
                // as a candidate.
                while(MyProceduralIntersectionEnumerator(
                    tHit,
                    candidateAttribs,
                    q.CandidateInstanceIndex(),
                    q.CandidatePrimitiveIndex(),
                    q.CandidateGeometryIndex()))
                {
                    if( (q.RayTMin() <= tHit) && (tHit <= q.CommittedRayT()) )
                    {
                        if(q.CandidateProceduralPrimitiveNonOpaque() &&
                            !MyProceduralAlphaTestLogic(
                                tHit,
                                candidateAttribs,
                                q.CandidateInstanceIndex(),
                                q.CandidatePrimitiveIndex(),
                                q.CandidateGeometryIndex()))
                        {
                            continue; // non opaque
                        }

                        q.CommitProceduralPrimitiveHit(tHit);
                        committedCustomAttribs = candidateAttribs;
                    }
                }
                break;
            }
            case CANDIDATE_NON_OPAQUE_TRIANGLE:
            {
                if( MyAlphaTestLogic(
                    q.CandidateInstanceIndex(),
                    q.CandidatePrimitiveIndex(),
                    q.CandidateGeometryIndex(),
                    q.CandidateTriangleRayT(),
                    q.CandidateTriangleBarycentrics(),
                    q.CandidateTriangleFrontFace()))
                {
                    q.CommitNonOpaqueTriangleHit();
                }
                if(MyLogicSaysStopSearchingForSomeReason()) // not typically applicable
                {
                    q.Abort(); // Stop traversing and next call to Proceed()
                               // will return FALSE.
                               // Post-traversal results will just be based
                               // on what has been encountered so far.
                }
                break;
            }
        }
    }
    switch(q.CommittedStatus())
    {
        case COMMITTED_TRIANGLE_HIT:
        {
            // Do hit shading
            CommonHit(rtParameters, 
                q.CommittedInstanceIndex(),
                q.CommittedPrimitiveIndex(),
                q.CommittedGeometryIndex(),
                q.CommittedTriangleBarycentrics(),
                q.WorldRayOrigin(),
                q.WorldRayDirection(),
                q.CommittedRayT(),
                q.CandidateTriangleFrontFace(),
                Payload);
            break;
        }
        case COMMITTED_PROCEDURAL_PRIMITIVE_HIT:
        {
            // Do hit shading for procedural hit,
            // using manually saved hit attributes (customAttribs)
            ShadeMyProceduralPrimitiveHit(
                committedCustomAttribs,
                q.CommittedInstanceIndex(),
                q.CommittedPrimitiveIndex(),
                q.CommittedGeometryIndex(),
                q.CommittedRayT());
            break;
        }
        case COMMITTED_NOTHING:
        {
            // Do miss shading
            CommonMiss(
                q.WorldRayOrigin(),
                q.WorldRayDirection(),
                q.CommittedRayT(),
                Payload);
            break;
        }
    }
#endif
}

HLSL::GIReservoir Validate(HLSL::RTParameters rtParameters, SurfaceData s, uint seed, float3 origin, HLSL::GIReservoir r, in HLSL::GIReservoir og, uint2 dtid)
{
    
    RESTIRRay restirRay;
    restirRay.Origin = origin;
    restirRay.Direction = normalize(r.hit_Wsum.xyz - origin);
    restirRay = IndirectLight(rtParameters, s, restirRay, 0, seed);
    
    float W = dot(restirRay.HitRadiance.xyz, float3(0.3, 0.59, 0.11));
    
    float distDiff = length(r.hit_Wsum.xyz - restirRay.HitPosition);
    float wDiff = 0;//saturate(abs(W - r.color_W.w));
    float likeness = 1.0f-saturate(distDiff + wDiff);
    
    float fail = likeness < 0.99 ? 1 : 0;
    if(fail)
    {
        r = og;
        //r.hit_Wsum.w /= r.dir_Wcount.w;
        //r.dir_Wcount.w = 1;
        
        HLSL::GIReservoir newR;
        newR.color_W = float4(restirRay.HitRadiance.xyz, W);
        newR.dir_Wcount = float4(restirRay.HitNormal, r.dir_Wcount.w);
        newR.hit_Wsum = float4(restirRay.HitPosition, r.hit_Wsum.w);
        
        UpdateGIReservoir(r, newR, nextRand(seed));
    }
    
    // ca sert a rien de scale le spacial... il est temporaire
    float frameFilteringCount = lerp(2, maxFrameFilteringCount * 64, likeness);
    //ScaleGIReservoir(r, frameFilteringCount);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    float3 gi = GI[dtid.xy];
    gi = pow(distDiff, 1);
    gi = 1-fail;
    //GI[dtid.xy] = gi;
    
    return r;
}

// ----------------------------- DEBUG ----------------------------------

#ifdef RAY_DISPATCH
void DebugSurfaceData(float3 pos, float3 dir)
{
    RaytracingAccelerationStructure BVH = ResourceDescriptorHeap[rtParameters.BVH];
    
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0;
    ray.TMax = 100000;
    
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint type = 1;
    uint depth = 0;
    uint seed = initRand(launchIndex.xy);
    
    HLSL::HitInfo payload;
    payload.color = float3(0.0, 0.0, 0.0);
    payload.type = 1; //indirect
    payload.depth = 0;
    payload.seed = seed;
    
    TraceRay( BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    
    RWTexture2D<float3> GI = ResourceDescriptorHeap[rtParameters.giIndex];
    GI[launchIndex] = payload.color;
}
#endif

// world space positions
void DrawLine(float3 begin, float3 end)
{
    RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[editorContext.debugVerticesCountHeapIndex];
    uint index = 0;
    counter[0] = 1;
    InterlockedAdd(counter[1], 2, index);
    
    RWStructuredBuffer<HLSL::Vertex> debugVertices = ResourceDescriptorHeap[editorContext.debugVerticesHeapIndex];
    HLSL::Vertex vertex1;
    HLSL::Vertex vertex2;
    
    vertex1.pos = begin;
    vertex1.normal = RandUINT(asuint(Rand(begin)));
    vertex1.uv = 0;
    
    vertex2.pos = end;
    vertex2.normal = vertex1.normal;
    vertex2.uv = 0;
    
    debugVertices[index] = vertex1;
    debugVertices[index+1] = vertex2;
    
    RWStructuredBuffer<HLSL::IndirectCommand> debugIndirects = ResourceDescriptorHeap[editorContext.debugBufferHeapIndex];
    HLSL::IndirectCommand di = debugIndirects[counter[0]-1];
    
	//di.cbv = debugParameters.cbv;
	//di.index = 0;
    //di.stuff = 0;
	di.drawArguments.InstanceCount = 1;
    di.drawArguments.VertexCountPerInstance = index+2;
	//di.drawArguments.IndexCountPerInstance = index+2;
	//di.drawArguments.StartIndexLocation = 0;
	//di.drawArguments.BaseVertexLocation = 0;
	//di.drawArguments.StartInstanceLocation = 0;
    
    debugIndirects[counter[0]-1] = di;
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
*/
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