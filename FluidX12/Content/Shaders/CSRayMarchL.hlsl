//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#include "RayMarch.hlsli"

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

	//rayOrigin.xyz = mul(rayOrigin, g_world);	// Light-map space to world space

	// Transmittance
#ifdef _HAS_SHADOW_MAP_
	min16float shadow = ShadowTest(mul(rayOrigin, g_world), g_txDepth);
#else
	min16float shadow = 1.0;
#endif

	// Light-map space same to volume space (coupled)
	//rayOrigin.xyz = mul(rayOrigin, g_worldI);	// World space to volume space
	const float3 uvw = LocalToTex3DSpace(rayOrigin.xyz);
	const min16float density = GetSample(uvw).w;

#ifdef _HAS_LIGHT_PROBE_
	min16float ao = 1.0;
	float3 irradiance = 0.0;
#endif

	if (density >= ZERO_THRESHOLD)
	{
		if (shadow >= ZERO_THRESHOLD)
		{
#ifdef _POINT_LIGHT_
			const float3 localSpaceLightPt = mul(float4(g_lightPt, 1.0), g_worldI);
			const float3 rayDir = normalize(localSpaceLightPt - rayOrigin.xyz);
#else
			const float3 localSpaceLightPt = mul(g_lightPt, (float3x3)g_worldI);
			const float3 rayDir = normalize(localSpaceLightPt);
#endif
			CastLightRay(shadow, rayOrigin.xyz, rayDir, g_step, g_numSamples);
		}

#ifdef _HAS_LIGHT_PROBE_
		if (g_hasLightProbes) // An approximation to GI effect with light probe
		{
			float3 shCoeffs[SH_NUM_COEFF];
			LoadSH(shCoeffs, g_roSHCoeffs);
			float3 rayDir = -GetDensityGradient(uvw);
			rayDir = any(abs(rayDir) > 0.0) ? rayDir : rayOrigin.xyz; // Avoid 0-gradient caused by uniform density field
			irradiance = GetIrradiance(shCoeffs, normalize(mul(rayDir, (float3x3)g_world)));
			rayDir = normalize(rayDir);
			CastLightRay(ao, rayOrigin.xyz, rayDir, g_step, g_numSamples);
		}
#endif
	}

	const min16float3 lightColor = min16float3(g_lightColor.xyz * g_lightColor.w);
	min16float3 ambient = min16float3(g_ambient.xyz * g_ambient.w);

#ifdef _HAS_LIGHT_PROBE_
	ambient = g_hasLightProbes ? ao * min16float3(irradiance) : ambient;
#endif

	g_rwLightMap[DTid] = shadow * lightColor + ambient;
}
