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

	// In-scattered radiance with inverted transmittance
	min16float4 scatter = 0.0;

	float t = 0.0;
	min16float step = g_step;
	float prevDensity = 0.0;
	for (uint i = 0; i < g_numSamples; ++i)
	{
		const float3 pos = rayOrigin + rayDir * t;
		if (any(abs(pos) > 1.0)) break;
		const float3 uvw = LocalToTex3DSpace(pos);

		// Get a sample
		//float mip = max(0.5 - transm, 0.0) * 2.0;
		//const float mip1 = WaveReadLaneAt(mip, couple);
		//mip = min(mip, mip1);
		//min16float4 color = GetSample(uvw, mip);
		min16float4 color = GetSample(uvw);
		min16float newStep = g_step;

		// Skip empty space
		if (color.w > ZERO_THRESHOLD)
		{
#ifdef _POINT_LIGHT_
			// Point light direction in texture space
			const float3 lightDir = normalize(localSpaceLightPt - pos);
#endif
			const float3 light = GetLight(pos, lightDir, shCoeffs); // Sample light

			// Update step
			const min16float transm = 1.0 - scatter.w;
			const float dDensity = color.w - prevDensity;
			newStep = GetStep(dDensity, transm, color.w, g_step);
			step = (step + newStep) * 0.5;
			prevDensity = color.w;

			// Accumulate color
#ifndef _PRE_MULTIPLIED_
			color.xyz *= color.w;
#endif
			color.xyz *= min16float3(light);
			scatter += color * ABSORPTION * transm;

			if (transm < ZERO_THRESHOLD) break;
		}

		// Update position along ray
		step = newStep;
		t += step;
#ifdef _HAS_DEPTH_MAP_
		if (t > tMax) break;
#endif
	}

	scatter.xyz /= 2.0 * PI;

	return scatter;
}
