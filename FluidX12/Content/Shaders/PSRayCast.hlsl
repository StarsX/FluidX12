//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#ifndef _LIGHT_PASS_
#include "RayMarch.hlsli"
#endif

struct PSIn
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

static const min16float g_stepScale = g_maxDist / NUM_SAMPLES;
static const min16float g_lightStepScale = g_maxDist / NUM_LIGHT_SAMPLES;

// Screen space to loacal space
//--------------------------------------------------------------------------------------
float3 TexcoordToLocalPos(float2 tex)
{
	float4 pos;
	pos.xy = tex * 2.0 - 1.0;
	pos.zw = float2(0.0, 1.0);
	pos.y = -pos.y;
	pos = mul(pos, g_worldViewProjI);

	return pos.xyz / pos.w;
}

//--------------------------------------------------------------------------------------
// Compute start point of the ray
//--------------------------------------------------------------------------------------
bool ComputeStartPoint(inout float3 pos, float3 rayDir)
{
	if (all(abs(pos) <= 1.0)) return true;

	//float U = asfloat(0x7f800000);	// INF
	float U = 3.402823466e+38;			// FLT_MAX
	bool isHit = false;

	[unroll]
	for (uint i = 0; i < 3; ++i)
	{
		const float u = (-sign(rayDir[i]) - pos[i]) / rayDir[i];
		if (u < 0.0h) continue;

		const uint j = (i + 1) % 3, k = (i + 2) % 3;
		if (abs(rayDir[j] * u + pos[j]) > 1.0) continue;
		if (abs(rayDir[k] * u + pos[k]) > 1.0) continue;
		if (u < U)
		{
			U = u;
			isHit = true;
		}
	}

	pos = clamp(rayDir * U + pos, -1.0, 1.0);

	return isHit;
}

#ifndef _LIGHT_PASS_
float3 GetLight(float3 pos, float3 step)
{
	min16float shadow = 1.0;	// Transmittance along light ray

	for (uint i = 0; i < NUM_LIGHT_SAMPLES; ++i)
	{
		// Update position along light ray
		pos += step;
		if (any(abs(pos) > 1.0)) break;
		float3 tex = pos * min16float3(0.5, -0.5, 0.5) + 0.5;

		// Get a sample along light ray
		const min16float density = GetSample(tex).w;

		// Attenuate ray-throughput along light direction
		shadow *= saturate(1.0 - GetOpacity(density, g_lightStepScale));
		if (shadow < ZERO_THRESHOLD) break;
	}

	return float3(shadow, shadow, shadow);
}
#endif

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
min16float4 main(PSIn input) : SV_TARGET
{
	float3 rayOrigin = TexcoordToLocalPos(input.Tex);		// The point on the near plane

	const float3 localSpaceEyePt = mul(g_eyePos, g_worldI).xyz;
	const float3 rayDir = normalize(rayOrigin - localSpaceEyePt);
	if (!ComputeStartPoint(rayOrigin, rayDir)) discard;

	const float3 step = rayDir * g_stepScale;

#ifdef _POINT_LIGHT_
	const float3 localSpaceLightPt = mul(g_lightPos, g_worldI).xyz;
#else
	const float3 localSpaceLightPt = mul(g_lightPos.xyz, (float3x3)g_worldI);
	const float3 lightStep = normalize(localSpaceLightPt) * g_lightStepScale;
#endif

	// Transmittance
	min16float transmit = 1.0;
	// In-scattered radiance
	min16float3 ambient = 0.0;
	min16float3 scatter = 0.0;
	float t = 0.0;
	for (uint i = 0; i < NUM_SAMPLES; ++i)
	{
		const float3 pos = rayOrigin + rayDir * t;
		if (any(abs(pos) > 1.0)) break;
		float3 tex = float3(0.5, -0.5, 0.5) * pos + 0.5;

		// Get a sample
		min16float4 color = GetSample(tex);

		// Skip empty space
		if (color.w > ZERO_THRESHOLD)
		{
			// Attenuate ray-throughput
			const min16float4 scaledColor = color * g_stepScale;
			transmit *= saturate(1.0 - scaledColor.w * ABSORPTION);
			if (transmit < ZERO_THRESHOLD) break;

			// Point light direction in texture space
#ifdef _POINT_LIGHT_
			const float3 lightStep = normalize(g_localSpaceLightPt - pos) * g_lightStepScale;
#endif
			const float3 light = GetLight(pos, lightStep);

			scatter += light * transmit * scaledColor.xyz;
			ambient += transmit * scaledColor.xyz;
		}
		t += max(1.5 * g_stepScale * t, g_stepScale);
	}

	min16float3 result = scatter + lerp(ambient, 1.0, 0.75) * 0.16;

	return min16float4(sqrt(result), saturate(1.0 - transmit));
}
