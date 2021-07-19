//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "RayMarch.hlsli"

//--------------------------------------------------------------------------------------
// Constant
//--------------------------------------------------------------------------------------
static const min16float g_stepScale = g_maxDist / NUM_LIGHT_SAMPLES;
static const min16float g_lightStepScale = g_maxDist / NUM_LIGHT_SAMPLES;
//--------------------------------------------------------------------------------------
// Unordered access texture
//--------------------------------------------------------------------------------------
RWTexture3D<float3> g_rwLightMap;

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------
[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float3 gridSize;
	g_rwLightMap.GetDimensions(gridSize.x, gridSize.y, gridSize.z);

	float4 rayOrigin;
	rayOrigin.xyz = (DTid + 0.5) / gridSize * 2.0 - 1.0;
	rayOrigin.w = 1.0;

	rayOrigin = mul(rayOrigin, g_lightMapWorld);	// Light-map space to world space
	rayOrigin = mul(rayOrigin, g_worldI);			// World space to volume space

	float3 tex = rayOrigin.xyz * 0.5 + 0.5;
	min16float density;// = GetSample(tex).w;
	/*if (density < ZERO_THRESHOLD)
	{
		g_rwLightMap[DTid] = 0.0;
		return;
	}*/

#ifdef _POINT_LIGHT_
	const float3 localSpaceLightPt = mul(g_lightPos, g_worldI).xyz;
	const float3 rayDir = normalize(localSpaceLightPt - pos);
#else
	const float3 localSpaceLightPt = mul(g_lightPos.xyz, (float3x3)g_worldI);
	const float3 rayDir = normalize(localSpaceLightPt);
#endif

	// Transmittance
	min16float shadow = 1.0;

	float t = 0.0;
	for (uint i = 0; i < NUM_LIGHT_SAMPLES; ++i)
	{
		const float3 pos = rayOrigin.xyz + rayDir * t;
		if (any(abs(pos) > 1.0)) break;
		tex = pos * min16float3(0.5, -0.5, 0.5) + 0.5;

		// Get a sample along light ray
		density = GetSample(tex).w;

		// Attenuate ray-throughput along light direction
		shadow *= 1.0 - GetOpacity(density, g_lightStepScale);
		if (shadow < ZERO_THRESHOLD) break;

		// Update position along light ray
		t += g_stepScale;
	}
	
	const min16float3 lightColor = min16float3(g_lightColor.xyz * g_lightColor.w);
	const min16float3 ambient = min16float3(g_ambient.xyz * g_ambient.w);
	g_rwLightMap[DTid] = float3(shadow, shadow, shadow);
}
