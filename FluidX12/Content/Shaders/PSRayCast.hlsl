//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#include "RayMarch.hlsli"

struct PSIn
{
	float4 Pos	: SV_POSITION;
	float2 UV	: TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Screen space to local space
//--------------------------------------------------------------------------------------
float3 TexcoordToLocalPos(float2 uv)
{
	float4 pos;
	pos.xy = uv * 2.0 - 1.0;
	pos.zw = float2(0.0, 1.0);
	pos.y = -pos.y;
	pos = mul(pos, g_worldViewProjI);

	return pos.xyz / pos.w;
}

//--------------------------------------------------------------------------------------
// Get clip-space position
//--------------------------------------------------------------------------------------
#ifdef _HAS_DEPTH_MAP_
float3 GetClipPos(uint2 idx, float2 uv)
{
	const float z = g_txDepth[idx];
	float2 xy = uv * 2.0 - 1.0;
	xy.y = -xy.y;

	return float3(xy, z);
}
#endif

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
min16float4 main(PSIn input) : SV_TARGET
{
	float3 rayOrigin = TexcoordToLocalPos(input.UV);	// The point on the near plane
	const float3 localSpaceEyePt = mul(float4(g_eyePt, 1.0), g_worldI);

	const float3 rayDir = normalize(rayOrigin - localSpaceEyePt);
	if (!ComputeRayOrigin(rayOrigin, rayDir)) discard;

#ifdef _HAS_DEPTH_MAP_
	// Calculate occluded end point
	const float3 pos = GetClipPos(input.Pos.xy, input.UV);
	const float tMax = GetTMax(pos, rayOrigin, rayDir);
#endif

	float3 shCoeffs[SH_NUM_COEFF];
#if defined(_HAS_LIGHT_PROBE_) && !defined(_LIGHT_PASS_)
	if (g_hasLightProbes) LoadSH(shCoeffs, g_roSHCoeffs);
#endif

#ifdef _POINT_LIGHT_
	const float3 localSpaceLightPt = mul(float4(g_lightPt, 1.0), g_worldI);
#else
	const float3 localSpaceLightPt = mul(g_lightPt, (float3x3)g_worldI);
	const float3 lightDir = normalize(localSpaceLightPt);
#endif

	// Transmittance
	min16float transm = 1.0;

	// In-scattered radiance
	min16float3 scatter = 0.0;

	float t = 0.0;
	min16float step = g_stepScale;
	for (uint i = 0; i < g_numSamples; ++i)
	{
		const float3 pos = rayOrigin + rayDir * t;
		if (any(abs(pos) > 1.0)) break;
		const float3 uvw = LocalToTex3DSpace(pos);

		// Get a sample
		min16float4 color = GetSample(uvw);

		// Skip empty space
		if (color.w > ZERO_THRESHOLD)
		{
#ifdef _POINT_LIGHT_
			// Point light direction in texture space
			const float3 lightDir = normalize(g_localSpaceLightPt - pos);
#endif
			// Sample light
			const float3 light = GetLight(pos, lightDir, shCoeffs);
			
			// Accumulate color
			color.w = GetOpacity(color.w, step);
			color.xyz *= transm;
#ifdef _PRE_MULTIPLIED_
			color.xyz = GetPremultiplied(color.xyz, step);
#else
			color.xyz *= color.w;
#endif

			//scatter += color.xyz;
			scatter += min16float3(light) * color.xyz;

			// Attenuate ray-throughput
			transm *= 1.0 - color.w;
			if (transm < ZERO_THRESHOLD) break;
		}

		step = min16float(max((1.0 - transm) * 2.0, 0.8)) * g_stepScale;
		step *= clamp(1.0 - color.w * 4.0, 0.5, 2.0);
		t += step;
#ifdef _HAS_DEPTH_MAP_
		if (t > tMax) break;
#endif
	}

	return min16float4(scatter, 1.0 - transm);
}
