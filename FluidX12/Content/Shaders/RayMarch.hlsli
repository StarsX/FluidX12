//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Common.hlsli"
#ifdef _HAS_LIGHT_PROBE_
#define SH_ORDER 3
#include "SHIrradiance.hlsli"
#endif

#define ABSORPTION			0.6
#define ZERO_THRESHOLD		0.01
#define ONE_THRESHOLD		0.99

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
cbuffer cbSampleRes
{
	uint g_numSamples;
#ifdef _HAS_LIGHT_PROBE_
	uint g_hasLightProbes;
#endif
	uint g_numLightSamples; // Only for non-light-separate paths, which need both view and light ray samples
};

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
static const min16float g_maxDist = 2.0 * sqrt(3.0);
static const min16float g_step = g_maxDist / min16float(g_numSamples);
static const min16float g_lightStep = g_maxDist / min16float(g_numLightSamples);

//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------
Texture3D g_txGrid;

#ifdef _LIGHT_PASS_
Texture3D<float3> g_txLightMap;
#endif

#ifdef _HAS_DEPTH_MAP_
Texture2D<float> g_txDepth;
#endif

#if defined(_HAS_SHADOW_MAP_) && !defined(_LIGHT_PASS_)
Texture2D<float> g_txShadow;
#endif

#if defined(_HAS_LIGHT_PROBE_) && !defined(_LIGHT_PASS_)
StructuredBuffer<float3> g_roSHCoeffs;
#endif

//--------------------------------------------------------------------------------------
// Sample density field
//--------------------------------------------------------------------------------------
min16float4 GetSample(float3 uvw, float mip = 0.0)
{
	min16float4 color = min16float4(g_txGrid.SampleLevel(g_smpLinear, uvw, mip));
	//min16float4 color = min16float4(0.0, 0.5, 1.0, 0.5);
#ifdef _PRE_MULTIPLIED_
	color.xyz *= DENSITY_SCALE;
#endif
	color.w *= DENSITY_SCALE;

	return color;
}

//--------------------------------------------------------------------------------------
// Sample density field
//--------------------------------------------------------------------------------------
float3 GetDensityGradient(float3 uvw)
{
	static const int3 offsets[] =
	{
		int3(-1, 0, 0),
		int3(1, 0, 0),
#ifdef _TEXCOORD_INVERT_Y_
		int3(0, 1, 0),
		int3(0, -1, 0),
#else
		int3(0, -1, 0),
		int3(0, 1, 0),
#endif
		int3(0, 0, -1),
		int3(0, 0, 1)
	};
	
	float q[6];
	[unroll]
	for (uint i = 0; i < 6; ++i) q[i] = g_txGrid.SampleLevel(g_smpLinear, uvw, 0.0, offsets[i]).w;

	return float3(q[1] - q[0], q[3] - q[2], q[5] - q[4]);
}

//--------------------------------------------------------------------------------------
// Get opacity
//--------------------------------------------------------------------------------------
min16float GetTranslucency(min16float density, min16float stepScale)
{
	return saturate(density * stepScale * ABSORPTION);
}

//--------------------------------------------------------------------------------------
// Get premultiplied color
//--------------------------------------------------------------------------------------
#ifdef _PRE_MULTIPLIED_
min16float3 GetPremultiplied(min16float3 color, min16float stepScale)
{
	return color * saturate(stepScale * ABSORPTION );
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

float GetTMax(float3 pos, float3 rayOrigin, float3 rayDir, float tMax)
{
	return min(GetTMax(pos, rayOrigin, rayDir), tMax);
}

//--------------------------------------------------------------------------------------
// Get occluded end point
//--------------------------------------------------------------------------------------
#ifdef _HAS_SHADOW_MAP_
min16float ShadowTest(float3 pos, Texture2D<float> txDepth)
{
	const float3 lsPos = mul(float4(pos, 1.0), g_shadowViewProj).xyz;
	float2 shadowUV = lsPos.xy * 0.5 + 0.5;
	shadowUV.y = 1.0 - shadowUV.y;

	const float depth = txDepth.SampleLevel(g_smpLinear, shadowUV, 0.0);

	return lsPos.z < depth;
}
#endif

//--------------------------------------------------------------------------------------
// Get irradiance
//--------------------------------------------------------------------------------------
#if defined(_HAS_LIGHT_PROBE_) && !defined(_LIGHT_PASS_)
float3 GetIrradiance(float3 shCoeffs[SH_NUM_COEFF], float3 dir)
{
	return EvaluateSHIrradiance(shCoeffs, normalize(dir)).xyz;
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
// Compute the end point of the ray
//--------------------------------------------------------------------------------------
float ComputeTargetHit(float3 rayOrigin, float3 target, float3 rayDir)
{
	const float3 u = (target - rayOrigin) / rayDir;

	return max(max(u.x, u.y), u.z);
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
// Get step
//--------------------------------------------------------------------------------------
min16float GetStep(float dDensity, min16float transm, min16float opacity, min16float step)
{
#if 1
	step *= min16float(clamp(0.5 / abs(dDensity), 1.0, 1.25));
	step *= max((1.0 - transm) * 2.0, 1.0);
	step *= max((1.0 - opacity) * 1.25, 1.0);
#endif

	return step;
}

//--------------------------------------------------------------------------------------
// Cast light ray
//--------------------------------------------------------------------------------------
void CastLightRay(inout min16float transm, float3 rayOrigin, float3 rayDir,
	min16float stepScale, uint numSamples, uint mipLevel = 0)
{
	const float mip = mipLevel;
	numSamples >>= mipLevel;

	float t = stepScale;
	min16float step = stepScale;
	float prevDensity = 0.0;
	for (uint i = 0; i < numSamples; ++i)
	{
		const float3 pos = rayOrigin + rayDir * t;
		if (any(abs(pos) > 1.0)) break;
		const float3 uvw = LocalToTex3DSpace(pos);

		// Get a sample along light ray
		const min16float density = GetSample(uvw, mip).w;

		// Update step
		const float dDensity = density - prevDensity;
		const min16float opacity = saturate(density * step);
		const min16float newStep = GetStep(dDensity, transm, opacity, stepScale);
		step = (step + newStep) * 0.5;
		prevDensity = density;

		// Attenuate ray-throughput along light direction
		const min16float tansl = GetTranslucency(density, step);
		transm *= 1.0 - tansl;
		if (transm < ZERO_THRESHOLD) break;

		// Update position along light ray
		step = newStep;
		t += step;
	}
}

//--------------------------------------------------------------------------------------
// Get light
//--------------------------------------------------------------------------------------
#ifdef _LIGHT_PASS_
float3 GetLight(float3 pos, float3 rayDir, float3 shCoeffs[SH_NUM_COEFF])
{
	const float3 uvw = pos * 0.5 + 0.5;

	return g_txLightMap.SampleLevel(g_smpLinear, uvw, 0.0);
}
#else
float3 GetLight(float3 pos, float3 lightDir, float3 shCoeffs[SH_NUM_COEFF])
{
	// Transmittance along light ray
#if defined(_HAS_SHADOW_MAP_) && !defined(_LIGHT_PASS_)
	min16float shadow = ShadowTest(mul(float4(pos, 1.0), g_world), g_txShadow);
#else
	min16float shadow = 1.0;
#endif

	if (shadow > ZERO_THRESHOLD)
		CastLightRay(shadow, pos, lightDir, g_lightStep, g_numLightSamples);

#ifdef _HAS_LIGHT_PROBE_
	min16float ao = 1.0;
	float3 irradiance = 0.0;
	if (g_hasLightProbes) // An approximation to GI effect with light probe
	{
		const float3 uvw = LocalToTex3DSpace(pos);
		float3 rayDir = -GetDensityGradient(uvw);
		rayDir = any(abs(rayDir) > 0.0) ? rayDir : pos; // Avoid 0-gradient caused by uniform density field
		irradiance = GetIrradiance(shCoeffs, normalize(mul(rayDir, (float3x3)g_world)));
		rayDir = normalize(rayDir);
		CastLightRay(ao, pos, rayDir, g_lightStep, g_numLightSamples);
	}
#endif

	const min16float3 lightColor = min16float3(g_lightColor.xyz * g_lightColor.w);
	min16float3 ambient = min16float3(g_ambient.xyz * g_ambient.w);

#ifdef _HAS_LIGHT_PROBE_
	ambient = g_hasLightProbes ? min16float3(irradiance) * ao : ambient;
#endif

	return lightColor * shadow + ambient;
}
#endif
