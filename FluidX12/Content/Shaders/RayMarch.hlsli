//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "RayCast.hlsli"

#define NUM_SAMPLES			256
#define NUM_LIGHT_SAMPLES	128
#define ABSORPTION			1.0
#define ZERO_THRESHOLD		0.01
#define ONE_THRESHOLD		0.99

//--------------------------------------------------------------------------------------
// Constant
//--------------------------------------------------------------------------------------
static const min16float g_maxDist = 2.0 * sqrt(3.0);
static const min16float g_lightStepScale = g_maxDist / NUM_LIGHT_SAMPLES;

//--------------------------------------------------------------------------------------
// Texture
//--------------------------------------------------------------------------------------
Texture3D<float4> g_txGrid;

#ifdef _LIGHT_PASS_
Texture3D<float3> g_txLightMap;
#endif

//--------------------------------------------------------------------------------------
// Sample density field
//--------------------------------------------------------------------------------------
min16float4 GetSample(float3 tex)
{
	min16float4 color = min16float4(g_txGrid.SampleLevel(g_smpLinear, tex, 0.0));
	color.w *= 24.0;

	return min(min16float4(color.xyz * color.w, color.w), 24.0);
}

//--------------------------------------------------------------------------------------
// Get opacity
//--------------------------------------------------------------------------------------
min16float GetOpacity(min16float density, min16float stepScale)
{
	return saturate(density * stepScale * ABSORPTION);
}

//--------------------------------------------------------------------------------------
// Compute start point of the ray
//--------------------------------------------------------------------------------------
bool ComputeRayOrigin(inout float3 rayOrigin, float3 rayDir)
{
	if (all(abs(rayOrigin) <= 1.0)) return true;

	//float U = asfloat(0x7f800000);	// INF
	float U = 3.402823466e+38;			// FLT_MAX
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
// Get light
//--------------------------------------------------------------------------------------
#ifdef _LIGHT_PASS_
float3 GetLight(float3 pos, float3 step)
{
	const float3 uvw = (pos + step) * 0.5 + 0.5;

	return g_txLightMap.SampleLevel(g_smpLinear, uvw, 0.0);
}
#else
float3 GetLight(float3 pos, float3 step)
{
	min16float shadow = 1.0; // Transmittance along light ray

	if (shadow > 0.0)
	{
		for (uint i = 0; i < NUM_LIGHT_SAMPLES; ++i)
		{
			// Update position along light ray
			pos += step;
			if (any(abs(pos) > 1.0)) break;
			const float3 uvw = pos * 0.5 + 0.5;

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
