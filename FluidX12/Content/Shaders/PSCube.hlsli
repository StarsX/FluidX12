//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConsts.h"
#include "RayCast.hlsli"

//--------------------------------------------------------------------------------------
// Texture
//--------------------------------------------------------------------------------------
#if _USE_PURE_ARRAY_
Texture2DArray<float4> g_txCubeMap;
#else
TextureCube<float4> g_txCubeMap;
#endif

#ifdef _HAS_DEPTH_MAP_

#if _USE_PURE_ARRAY_
Texture2DArray<float> g_txCubeDepth;
#else
TextureCube<float4> g_txCubeDepth;
#endif

Texture2D<float> g_txDepth;
#endif

//--------------------------------------------------------------------------------------
// Unproject and return z in viewing space
//--------------------------------------------------------------------------------------
float UnprojectZ(float depth)
{
	static const float3 unproj = { g_zNear - g_zFar, g_zFar, g_zNear * g_zFar };

	return unproj.z / (depth * unproj.x + unproj.y);
}

//--------------------------------------------------------------------------------------
// Get domain location
//--------------------------------------------------------------------------------------
min16float2 GetDomain(float2 uv, float3 pos, float3 rayDir, float2 gridSize)
{
	uv *= gridSize;
	min16float2 domain = min16float2(frac(uv + 0.5));

#if !_USE_PURE_ARRAY_
	const float bound = gridSize.x - 1.0;
	const float3 axes = pos * gridSize.x;
	if (any(abs(axes) > bound && axes * rayDir < 0.0))
	{
		// Need to clamp the exterior edge
		uv = min(uv, gridSize - 0.5);
		domain = uv < 0.5 ? 1.0 : 0.0;
	}
#endif

	return domain;
}

//--------------------------------------------------------------------------------------
// Cube interior-surface casting
//--------------------------------------------------------------------------------------
min16float4 CubeCast(uint2 idx, float3 uvw, float3 pos, float3 rayDir)
{
	float2 gridSize;
	g_txCubeMap.GetDimensions(gridSize.x, gridSize.y);
	float2 uv = uvw.xy;

#if !_USE_PURE_ARRAY_
	uvw = pos;
#endif

	const float4 color = g_txCubeMap.SampleLevel(g_smpLinear, uvw, 0.0);
	const float4x4 gathers =
	{
		g_txCubeMap.GatherRed(g_smpLinear, uvw),
		g_txCubeMap.GatherGreen(g_smpLinear, uvw),
		g_txCubeMap.GatherBlue(g_smpLinear, uvw),
		g_txCubeMap.GatherAlpha(g_smpLinear, uvw)
	};

#ifdef _HAS_DEPTH_MAP_
	const float4 z = g_txCubeDepth.GatherRed(g_smpLinear, uvw);
	float depth = g_txDepth[idx];
#endif

	const min16float2 domain = GetDomain(uv, pos, rayDir, gridSize);
	const min16float2 domainInv = 1.0 - domain;
	const min16float4 wb =
	{
		domainInv.x * domain.y,
		domain.x * domain.y,
		domain.x * domainInv.y,
		domainInv.x * domainInv.y
	};

	const min16float4x4 samples = transpose(min16float4x4(gathers));
#ifdef _HAS_DEPTH_MAP_
	depth = UnprojectZ(depth);
#endif
	min16float4 result = 0.0;
	min16float ws = 0.0;
	[unroll]
	for (uint i = 0; i < 4; ++i)
	{
#ifdef _HAS_DEPTH_MAP_
		const float zi = UnprojectZ(z[i]);
		min16float w = min16float(max(1.0 - 0.5 * abs(depth - zi), 0.0));
		w *= wb[i];
#else
		const min16float w = wb[i];
#endif

		result += samples[i] * w;
		ws += w;
	}

	//result = min16float4(color); // Reference
	result = ws > 0.0 ? result / ws : min16float4(color);

	if (result.w <= 0.0) discard;

	return result;
}
