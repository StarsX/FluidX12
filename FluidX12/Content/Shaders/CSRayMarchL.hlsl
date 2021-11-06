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

	rayOrigin.xyz = mul(rayOrigin, g_lightMapWorld);	// Light-map space to world space

	// Transmittance
#ifdef _HAS_SHADOW_MAP_
	min16float shadow = ShadowTest(rayOrigin.xyz, g_txDepth);
#else
	min16float shadow = 1.0;
#endif

#ifdef _HAS_LIGHT_PROBE_
	min16float ao = 1.0;
	float3 irradiance = 0.0;
#endif

	rayOrigin.xyz = mul(rayOrigin, g_worldI);			// World space to volume space
	const float3 uvw = LocalToTex3DSpace(rayOrigin.xyz);
	const min16float density = GetSample(uvw).w;

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

			float t = g_stepScale;
			for (uint i = 0; i < g_numSamples; ++i)
			{
				const float3 pos = rayOrigin.xyz + rayDir * t;
				if (any(abs(pos) > 1.0)) break;
				const float3 uvw = LocalToTex3DSpace(pos);

				// Get a sample along light ray
				const min16float density = GetSample(uvw).w;

				// Attenuate ray-throughput along light direction
				shadow *= 1.0 - GetOpacity(density, g_stepScale);
				if (shadow < ZERO_THRESHOLD) break;

				// Update position along light ray
				t += g_stepScale;
			}
		}

#ifdef _HAS_LIGHT_PROBE_
		if (g_hasLightProbes)
		{
			float3 rayDir = -GetDensityGradient(uvw);
			irradiance = GetIrradiance(mul(rayDir, (float3x3)g_world));
			rayDir = normalize(rayDir);

			float t = g_stepScale;
			for (uint i = 0; i < g_numSamples; ++i)
			{
				const float3 pos = rayOrigin.xyz + rayDir * t;
				if (any(abs(pos) > 1.0)) break;
				const float3 uvw = LocalToTex3DSpace(pos);

				// Get a sample along light ray
				const min16float density = GetSample(uvw).w;

				// Attenuate ray-throughput along light direction
				ao *= 1.0 - GetOpacity(density, g_stepScale);
				if (ao < ZERO_THRESHOLD) break;

				// Update position along light ray
				t += g_stepScale;
			}
		}
#endif
	}

	const min16float3 lightColor = min16float3(g_lightColor.xyz * g_lightColor.w);
	min16float3 ambient = min16float3(g_ambient.xyz * g_ambient.w);

#ifdef _HAS_LIGHT_PROBE_
	ambient = g_hasLightProbes ? min16float3(irradiance) * ao : ambient;
#endif

	g_rwLightMap[DTid] = lightColor * shadow + ambient;

}
