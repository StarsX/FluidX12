//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "RayCast.hlsli"

#define ABSORPTION			1.0
#define ZERO_THRESHOLD		0.01
#define ONE_THRESHOLD		0.99

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
cbuffer cbSampleRes
{
	uint g_numSamples;
	uint g_numLightSamples; // Only for non-light-separate paths, which need both view and light ray samples
};

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
static const min16float g_maxDist = 2.0 * sqrt(3.0);
static const min16float g_stepScale = g_maxDist / min16float(g_numSamples);
static const min16float g_lightStepScale = g_maxDist / min16float(g_numLightSamples);

//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------
Texture3D<float4> g_txGrid;

#ifdef _LIGHT_PASS_
Texture3D<float3> g_txLightMap;
#endif

#ifdef _HAS_DEPTH_MAP_
Texture2D<float> g_txDepth;
#endif

#if defined(_HAS_SHADOW_MAP_) && !defined(_LIGHT_PASS_)
Texture2D<float> g_txShadow;
#endif

//--------------------------------------------------------------------------------------
// Sample density field
//--------------------------------------------------------------------------------------
min16float4 GetSample(float3 uvw)
{
	min16float4 color = min16float4(g_txGrid.SampleLevel(g_smpLinear, uvw, 0.0));
	//min16float4 color = min16float4(0.0, 0.5, 1.0, 0.5);
#ifdef _PRE_MULTIPLIED_
	color.xyz *= DENSITY_SCALE;
#endif
	color.w *= DENSITY_SCALE;

	return color;
}

//--------------------------------------------------------------------------------------
// Get opacity
//--------------------------------------------------------------------------------------
min16float GetOpacity(min16float density, min16float stepScale)
{
	return saturate(density * stepScale * ABSORPTION * 4.0);
}

//--------------------------------------------------------------------------------------
// Get opacity
//--------------------------------------------------------------------------------------
#ifdef _PRE_MULTIPLIED_
min16float3 GetPremultiplied(min16float3 color, min16float stepScale)
{
	return color * saturate(stepScale * ABSORPTION * 4.0);
}
#endif

//--------------------------------------------------------------------------------------
// Get occluded end point
//--------------------------------------------------------------------------------------
float GetTMax(float3 pos, float3 rayOrigin, float3 rayDir)
{
	if (pos.z >= 1.0) return FLT_MAX;

	const float4 hpos = mul(float4(pos, 1.0), g_worldViewProjI);
	pos = hpos.xyz / hpos.w;

	const float3 t = (pos - rayOrigin) / rayDir;

	return max(max(t.x, t.y), t.z);
}

//--------------------------------------------------------------------------------------
// Get occluded end point
//--------------------------------------------------------------------------------------
#ifdef _HAS_SHADOW_MAP_
min16float ShadowTest(float3 pos, Texture2D<float> txDepth)
{
	const float3 lsPos = mul(float4(pos, 1.0), g_shadowWVP).xyz;
	float2 shadowUV = lsPos.xy * 0.5 + 0.5;
	shadowUV.y = 1.0 - shadowUV.y;

	const float depth = txDepth.SampleLevel(g_smpLinear, shadowUV, 0.0);

	return lsPos.z < depth;
}
#endif

//--------------------------------------------------------------------------------------
// Compute start point of the ray
//--------------------------------------------------------------------------------------
bool ComputeRayOrigin(inout float3 rayOrigin, float3 rayDir)
{
	if (all(abs(rayOrigin) <= 1.0)) return true;

	//float U = INF;
	float U = FLT_MAX;
	bool isHit = false;

	[unroll]
	for (uint i = 0; i < 3; ++i)
	{
		const float u = (-sign(rayDir[i]) - rayOrigin[i]) / rayDir[i];
		if (u < 0.0) continue;

		const uint j = (i + 1) % 3, k = (i + 2) % 3;
		if (abs(rayDir[j] * u + rayOrigin[j]) > 1.0) continue;
		if (abs(rayDir[k] * u + rayOrigin[k]) > 1.0) continue;
		if (u < U)
		{
			U = u;
			isHit = true;
		}
	}

	rayOrigin = clamp(rayDir * U + rayOrigin, -1.0, 1.0);

	return isHit;
}

//--------------------------------------------------------------------------------------
// Local position to texture space
//--------------------------------------------------------------------------------------
float3 LocalToTex3DSpace(float3 pos)
{
#ifdef _TEXCOORD_INVERT_Y_
	return pos * float3(0.5, -0.5, 0.5) + 0.5;
#else
	return pos * 0.5 + 0.5;
#endif
}

//--------------------------------------------------------------------------------------
// Get light
//--------------------------------------------------------------------------------------
#ifdef _LIGHT_PASS_
float3 GetLight(float3 pos, float3 step)
{
	const float3 uvw = pos * 0.5 + 0.5;

	return g_txLightMap.SampleLevel(g_smpLinear, uvw, 0.0);
}
#else
float3 GetLight(float3 pos, float3 step)
{
	// Transmittance along light ray
#ifdef _HAS_SHADOW_MAP_
	min16float shadow = ShadowTest(pos, g_txShadow);
#else
	min16float shadow = 1.0;
#endif

	if (shadow > 0.0)
	{
		for (uint i = 0; i < g_numLightSamples; ++i)
		{
			// Update position along light ray
			pos += step;
			if (any(abs(pos) > 1.0)) break;
			const float3 uvw = LocalToTex3DSpace(pos);

			// Get a sample along light ray
			const min16float density = GetSample(uvw).w;

			// Attenuate ray-throughput along light direction
			shadow *= 1.0 - GetOpacity(density, g_lightStepScale);
			if (shadow < ZERO_THRESHOLD) break;
		}
	}

	const min16float3 lightColor = min16float3(g_lightColor.xyz * g_lightColor.w);
	const min16float3 ambient = min16float3(g_ambient.xyz * g_ambient.w);

	return lightColor * shadow + ambient;
}
#endif
