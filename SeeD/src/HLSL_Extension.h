#pragma once

#include <cmath>

namespace hlslpp
{

	inline float4 PlaneFromPointNormal(float3 Point, float3 Normal)
	{
		float W = -dot(Point, Normal);
		return float4(Normal, W);
	}

	//------------------------------------------------------------------------------

	inline float4 PlaneFromPoints(float3 Point1, float3 Point2, float3 Point3)
	{
		float3 V21 = Point1 - Point2;
		float3 V31 = Point1 - Point3;

		float3 N = cross(V21, V31);
		N = normalize(N);

		float D = -dot(N, Point1);

		float4 Result = float4(N, D);
		return Result;
	}

	// return >1 for the normal direction side or <1 when in the opposite side of the normal
	inline float PointPlaneSide(float3 point, float4 plane)
	{
		return dot(plane.xyz * plane.w, point.xyz);
	}

	inline float3 RandNormalize()
	{
		float x = (((float)rand() / (float)RAND_MAX) - 0.5f) * 2;
		float y = (((float)rand() / (float)RAND_MAX) - 0.5f) * 2;
		float z = (((float)rand() / (float)RAND_MAX) - 0.5f) * 2;
		return normalize(float3(x, y, z));
	}

	float4x4 Matrix(float3 position, quaternion rotation, float3 scale)
	{
		float4x4 rotationMat;
		float4x4 translationMat;
		float4x4 scaleMat;

		rotationMat = float4x4(rotation);
		translationMat = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, position.x, position.y, position.z, 1);
		scaleMat = float4x4(scale.x, 0, 0, 0, 0, scale.y, 0, 0, 0, 0, scale.z, 0, 0, 0, 0, 1);

		float4x4 matrix = mul(scaleMat, mul(rotationMat, translationMat));
		return matrix;
	}

	inline quaternion MatrixToQuaternion(float3x3 m)
	{
		quaternion q;
		float t;
		if (m[2][2] < 0) {
			if ((float)m[0][0] > (float)m[1][1]) {
				t = 1 + m[0][0] - m[1][1] - m[2][2];
				q = quaternion(t, m[0][1] + m[1][0], m[2][0] + m[0][2], m[1][2] - m[2][1]);
			}
			else {
				t = 1 - m[0][0] + m[1][1] - m[2][2];
				q = quaternion(m[0][1] + m[1][0], t, m[1][2] + m[2][1], m[2][0] - m[0][2]);
			}
		}
		else {
			if (m[0][0] < -m[1][1]) {
				t = 1 - m[0][0] - m[1][1] + m[2][2];
				q = quaternion(m[2][0] + m[0][2], m[1][2] + m[2][1], t, m[0][1] - m[1][0]);
			}
			else {
				t = 1 + m[0][0] + m[1][1] + m[2][2];
				q = quaternion(m[1][2] - m[2][1], m[2][0] - m[0][2], m[0][1] - m[1][0], t);
			}
		}
		q *= 0.5 / sqrt(float1(t));

		return q;
	}

	inline float3 ToEulerAngles(quaternion q) 
	{
		float3 angles;

		// roll (x-axis rotation)
		double sinr_cosp = 2 * (q.w * q.x + q.y * q.z);
		double cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
		angles.x = (float)std::atan2(sinr_cosp, cosr_cosp);

		// pitch (y-axis rotation)
		double sinp = 2 * (q.w * q.y - q.z * q.x);
		if (std::abs(sinp) >= 1)
			angles.y = (float)std::copysign(3.14159265f / 2, sinp); // use 90 degrees if out of range
		else
			angles.y = (float)std::asin(sinp);

		// yaw (z-axis rotation)
		double siny_cosp = 2 * (q.w * q.z + q.x * q.y);
		double cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
		angles.z = (float)std::atan2(siny_cosp, cosy_cosp);

		return angles;
	}

	/*
	// en degree
	quaternion EulertoQuaternion(float roll, float pitch, float yaw)
	{
		roll *= 3.1415f / 180;
		pitch *= 3.1415f / 180;
		yaw *= 3.1415f / 180;
	
		float t0 = std::cos(yaw * 0.5f);
		float t1 = std::sin(yaw * 0.5f);
		float t2 = std::cos(roll * 0.5f);
		float t3 = std::sin(roll * 0.5f);
		float t4 = std::cos(pitch * 0.5f);
		float t5 = std::sin(pitch * 0.5f);

		float w = t0 * t2 * t4 + t1 * t3 * t5;
		float x = t0 * t3 * t4 - t1 * t2 * t5;
		float y = t0 * t2 * t5 + t1 * t3 * t4;
		float z = t1 * t2 * t4 - t0 * t3 * t5;

		return quaternion(x, y, z, w);
	}
	*/


	// ADDED SeeD
	hlslpp_inline quaternion lookAt(const float3& forward, const float3& up)
	{
		//compute rotation axis
		float3 rotAxis = normalize(cross(float3(0, 0, 1), forward));
		if (length(rotAxis).x < 0.001f)
			rotAxis = up;

		//find the angle around rotation axis
		float1 dot2 = dot(float3(0, 0, 1), forward);
		float1 ang = acos(dot2);

		//convert axis angle to quaternion
		return quaternion::rotation_axis(rotAxis, ang);
	}

	static hlslpp_inline float4x4 lookAt(const float3& position, const float3& target, const float3& up)
	{
#if HLSLPP_COORDINATES == HLSLPP_COORDINATES_LEFT_HANDED
		const float3 look = normalize(target - position);
#else
		const float3 look = normalize(position - target); // Negate without the extra cost
#endif
		const float3 right = normalize(cross(up, look));
		const float3 up_dir = cross(look, right);

#if HLSLPP_LOGICAL_LAYOUT == HLSLPP_LOGICAL_LAYOUT_ROW_MAJOR
		return float4x4(
			float4(right.x, up_dir.x, look.x, 0.0f),
			float4(right.y, up_dir.y, look.y, 0.0f),
			float4(right.z, up_dir.z, look.z, 0.0f),
			float4(-dot(position, right), -dot(position, up_dir), -dot(position, look), 1.0f)
		);
#else
		return float4x4(
			float4(right, -dot(position, right)),
			float4(up_dir, -dot(position, up_dir)),
			float4(look, -dot(position, look)),
			float4(0.0f, 0.0f, 0.0f, 1.0f)
		);
#endif
	}

	const float XM_PI = 3.141592654f;
	const float XM_2PI = 6.283185307f;
	const float XM_1DIVPI = 0.318309886f;
	const float XM_1DIV2PI = 0.159154943f;
	const float XM_PIDIV2 = 1.570796327f;
	const float XM_PIDIV4 = 0.785398163f;
	inline void ScalarSinCos
	(
		float* pSin,
		float* pCos,
		float  Value
	)
	{
		//assert(pSin);
		//assert(pCos);

		// Map Value to y in [-pi,pi], x = 2*pi*quotient + remainder.
		float quotient = XM_1DIV2PI * Value;
		if (Value >= 0.0f)
		{
			quotient = static_cast<float>(static_cast<int>(quotient + 0.5f));
		}
		else
		{
			quotient = static_cast<float>(static_cast<int>(quotient - 0.5f));
		}
		float y = Value - XM_2PI * quotient;

		// Map y to [-pi/2,pi/2] with sin(y) = sin(Value).
		float sign;
		if (y > XM_PIDIV2)
		{
			y = XM_PI - y;
			sign = -1.0f;
		}
		else if (y < -XM_PIDIV2)
		{
			y = -XM_PI - y;
			sign = -1.0f;
		}
		else
		{
			sign = +1.0f;
		}

		float y2 = y * y;

		// 11-degree minimax approximation
		*pSin = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;

		// 10-degree minimax approximation
		float p = ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f;
		*pCos = sign * p;
	}

	inline float4x4 MatrixPerspectiveFovLH
	(
		float FovAngleY,
		float AspectRatio,
		float NearZ,
		float FarZ
	)
	{
		/*
		assert(NearZ > 0.f && FarZ > 0.f);
		assert(!XMScalarNearEqual(FovAngleY, 0.0f, 0.00001f * 2.0f));
		assert(!XMScalarNearEqual(AspectRatio, 0.0f, 0.00001f));
		assert(!XMScalarNearEqual(FarZ, NearZ, 0.00001f));
		*/


		float    SinFov = std::sinf(0.5f * FovAngleY);
		float    CosFov = std::cosf(0.5f * FovAngleY);
		//ScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);


		float Height = CosFov / SinFov;
		float Width = Height / AspectRatio;
		float fRange = FarZ / (FarZ - NearZ);

		float4x4 M;
		
		M[0].x = Width;
		M[0].y = 0.0f;
		M[0].z = 0.0f;
		M[0].w = 0.0f;

		M[1].x = 0.0f;
		M[1].y = Height;
		M[1].z = 0.0f;
		M[1].w = 0.0f;

		M[2].x = 0.0f; // il manque un truc ici ?
		M[2].y = 0.0f; // il manque un truc ici ?
		M[2].z = fRange;
		M[2].w = 1.0f;
		
		M[3].x = 0.0f;
		M[3].y = 0.0f;
		M[3].z = -fRange * NearZ;
		M[3].w = 0.0f;

		return M;

		/*
#if defined(_XM_NO_INTRINSICS_)

		float    SinFov;
		float    CosFov;
		XMScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

		float Height = CosFov / SinFov;
		float Width = Height / AspectRatio;
		float fRange = FarZ / (FarZ - NearZ);

		XMMATRIX M;
		M.m[0][0] = Width;
		M.m[0][1] = 0.0f;
		M.m[0][2] = 0.0f;
		M.m[0][3] = 0.0f;

		M.m[1][0] = 0.0f;
		M.m[1][1] = Height;
		M.m[1][2] = 0.0f;
		M.m[1][3] = 0.0f;

		M.m[2][0] = 0.0f;
		M.m[2][1] = 0.0f;
		M.m[2][2] = fRange;
		M.m[2][3] = 1.0f;

		M.m[3][0] = 0.0f;
		M.m[3][1] = 0.0f;
		M.m[3][2] = -fRange * NearZ;
		M.m[3][3] = 0.0f;
		return M;

#elif defined(_XM_ARM_NEON_INTRINSICS_)
		float    SinFov;
		float    CosFov;
		XMScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

		float fRange = FarZ / (FarZ - NearZ);
		float Height = CosFov / SinFov;
		float Width = Height / AspectRatio;
		const XMVECTOR Zero = vdupq_n_f32(0);

		XMMATRIX M;
		M.r[0] = vsetq_lane_f32(Width, Zero, 0);
		M.r[1] = vsetq_lane_f32(Height, Zero, 1);
		M.r[2] = vsetq_lane_f32(fRange, g_XMIdentityR3.v, 2);
		M.r[3] = vsetq_lane_f32(-fRange * NearZ, Zero, 2);
		return M;
#elif defined(_XM_SSE_INTRINSICS_)
		float    SinFov;
		float    CosFov;
		XMScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

		float fRange = FarZ / (FarZ - NearZ);
		// Note: This is recorded on the stack
		float Height = CosFov / SinFov;
		XMVECTOR rMem = {
			Height / AspectRatio,
			Height,
			fRange,
			-fRange * NearZ
		};
		// Copy from memory to SSE register
		XMVECTOR vValues = rMem;
		XMVECTOR vTemp = _mm_setzero_ps();
		// Copy x only
		vTemp = _mm_move_ss(vTemp, vValues);
		// CosFov / SinFov,0,0,0
		XMMATRIX M;
		M.r[0] = vTemp;
		// 0,Height / AspectRatio,0,0
		vTemp = vValues;
		vTemp = _mm_and_ps(vTemp, g_XMMaskY);
		M.r[1] = vTemp;
		// x=fRange,y=-fRange * NearZ,0,1.0f
		vTemp = _mm_setzero_ps();
		vValues = _mm_shuffle_ps(vValues, g_XMIdentityR3, _MM_SHUFFLE(3, 2, 3, 2));
		// 0,0,fRange,1.0f
		vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 0, 0, 0));
		M.r[2] = vTemp;
		// 0,0,-fRange * NearZ,0.0f
		vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 1, 0, 0));
		M.r[3] = vTemp;
		return M;
#endif
		*/
	}

	inline float4x4 MatrixOrthographicLH
	(
		float ViewWidth,
		float ViewHeight,
		float NearZ,
		float FarZ
	)
	{
		//assert(!XMScalarNearEqual(ViewWidth, 0.0f, 0.00001f));
		//assert(!XMScalarNearEqual(ViewHeight, 0.0f, 0.00001f));
		//assert(!XMScalarNearEqual(FarZ, NearZ, 0.00001f));

		float fRange = 1.0f / (FarZ - NearZ);

		float4x4 M;
		M[0].x = 2.0f / ViewWidth;
		M[0].y = 0.0f;
		M[0].z = 0.0f;
		M[0].w = 0.0f;

		M[1].x = 0.0f;
		M[1].y = 2.0f / ViewHeight;
		M[1].z = 0.0f;
		M[1].w = 0.0f;

		M[2].x = 0.0f;
		M[2].y = 0.0f;
		M[2].z = fRange;
		M[2].w = 0.0f;

		M[3].x = 0.0f;
		M[3].y = 0.0f;
		M[3].z = -fRange * NearZ;
		M[3].w = 1.0f;
		return M;
		/*
#if defined(_XM_NO_INTRINSICS_)

		float fRange = 1.0f / (FarZ - NearZ);

		XMMATRIX M;
		M.m[0][0] = 2.0f / ViewWidth;
		M.m[0][1] = 0.0f;
		M.m[0][2] = 0.0f;
		M.m[0][3] = 0.0f;

		M.m[1][0] = 0.0f;
		M.m[1][1] = 2.0f / ViewHeight;
		M.m[1][2] = 0.0f;
		M.m[1][3] = 0.0f;

		M.m[2][0] = 0.0f;
		M.m[2][1] = 0.0f;
		M.m[2][2] = fRange;
		M.m[2][3] = 0.0f;

		M.m[3][0] = 0.0f;
		M.m[3][1] = 0.0f;
		M.m[3][2] = -fRange * NearZ;
		M.m[3][3] = 1.0f;
		return M;

#elif defined(_XM_ARM_NEON_INTRINSICS_)
		float fRange = 1.0f / (FarZ - NearZ);

		const XMVECTOR Zero = vdupq_n_f32(0);
		XMMATRIX M;
		M.r[0] = vsetq_lane_f32(2.0f / ViewWidth, Zero, 0);
		M.r[1] = vsetq_lane_f32(2.0f / ViewHeight, Zero, 1);
		M.r[2] = vsetq_lane_f32(fRange, Zero, 2);
		M.r[3] = vsetq_lane_f32(-fRange * NearZ, g_XMIdentityR3.v, 2);
		return M;
#elif defined(_XM_SSE_INTRINSICS_)
		XMMATRIX M;
		float fRange = 1.0f / (FarZ - NearZ);
		// Note: This is recorded on the stack
		XMVECTOR rMem = {
			2.0f / ViewWidth,
			2.0f / ViewHeight,
			fRange,
			-fRange * NearZ
		};
		// Copy from memory to SSE register
		XMVECTOR vValues = rMem;
		XMVECTOR vTemp = _mm_setzero_ps();
		// Copy x only
		vTemp = _mm_move_ss(vTemp, vValues);
		// 2.0f / ViewWidth,0,0,0
		M.r[0] = vTemp;
		// 0,2.0f / ViewHeight,0,0
		vTemp = vValues;
		vTemp = _mm_and_ps(vTemp, g_XMMaskY);
		M.r[1] = vTemp;
		// x=fRange,y=-fRange * NearZ,0,1.0f
		vTemp = _mm_setzero_ps();
		vValues = _mm_shuffle_ps(vValues, g_XMIdentityR3, _MM_SHUFFLE(3, 2, 3, 2));
		// 0,0,fRange,0.0f
		vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 0, 0, 0));
		M.r[2] = vTemp;
		// 0,0,-fRange * NearZ,1.0f
		vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 1, 0, 0));
		M.r[3] = vTemp;
		return M;
#endif
		*/
	}
}