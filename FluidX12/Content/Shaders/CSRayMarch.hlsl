//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConsts.h"
#include "RayMarch.hlsli"

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
#if _CPU_CUBE_FACE_CULL_ == 1
cbuffer cb
{
	uint g_visibilityMask;
};
#elif _CPU_CUBE_FACE_CULL_ == 2
cbuffer cb
{
	uint g_faces[5];
};
#endif

//--------------------------------------------------------------------------------------
// Unordered access textures
//--------------------------------------------------------------------------------------
RWTexture2DArray<float4> g_rwCubeMap;
#ifdef _HAS_DEPTH_MAP_
RWTexture2DArray<float> g_rwCubeDepth;
#endif

//--------------------------------------------------------------------------------------
// Texture sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpPoint;

//--------------------------------------------------------------------------------------
// Get the local-space position of the grid surface
//--------------------------------------------------------------------------------------
float3 GetLocalPos(float2 pos, uint face, RWTexture2DArray<float4> rwCubeMap)
{
	float3 gridSize;
	rwCubeMap.GetDimensions(gridSize.x, gridSize.y, gridSize.z);
	
	pos = (pos + 0.5) / gridSize.xy * 2.0 - 1.0;
	pos.y = -pos.y;

	switch (face)
	{
	case 0: // +X
		return float3(1.0, pos.y, -pos.x);
	case 1: // -X
		return float3(-1.0, pos.y, pos.x);
	case 2: // +Y
		return float3(pos.x, 1.0, -pos.y);
	case 3: // -Y
		return float3(pos.x, -1.0, pos.y);
	case 4: // +Z
		return float3(pos.x, pos.y, 1.0);
	case 5: // -Z
		return float3(-pos.x, pos.y, -1.0);
	default:
		return 0.0;
	}
}

//--------------------------------------------------------------------------------------
// Check the visibility of the cube face
//--------------------------------------------------------------------------------------
bool IsVisible(uint face, float3 localSpaceEyePt)
{
	const float viewComp = localSpaceEyePt[face >> 1];

	return (face & 0x1) ? viewComp > -1.0 : viewComp < 1.0;
}

//--------------------------------------------------------------------------------------
// Get clip-space position
//--------------------------------------------------------------------------------------
#ifdef _HAS_DEPTH_MAP_
float3 GetClipPos(float3 rayOrigin, float3 rayDir)
{
	float4 hPos = float4(rayOrigin + 0.01 * rayDir, 1.0);
	hPos = mul(hPos, g_worldViewProj);

	const float2 xy = hPos.xy / hPos.w;
	float2 uv = xy * 0.5 + 0.5;
	uv.y = 1.0 - uv.y;

	const float z = g_txDepth.SampleLevel(g_smpPoint, uv, 0.0);

	return float3(xy, z);
}
#endif

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
#if _CPU_CUBE_FACE_CULL_ == 1
	if ((g_visibilityMask & (1 << DTid.z)) == 0) return;
#elif _CPU_CUBE_FACE_CULL_ == 2
	DTid.z = g_faces[DTid.z];
#endif

	float3 rayOrigin = mul(float4(g_eyePt, 1.0), g_worldI);
	//if (rayOrigin[DTid.z >> 1] == 0.0) return;

#if !defined(_CPU_CUBE_FACE_CULL_) || _CPU_CUBE_FACE_CULL_ == 0
	if (!IsVisible(DTid.z, rayOrigin)) return;
#endif

	const float3 target = GetLocalPos(DTid.xy, DTid.z, g_rwCubeMap);
	const float3 rayDir = normalize(target - rayOrigin);
	if (!ComputeRayOrigin(rayOrigin, rayDir)) return;

	float tMax = ComputeTargetHit(rayOrigin, target, rayDir);
	const min16float stepScale = g_step;

#ifdef _HAS_DEPTH_MAP_
	// Calculate occluded end point
	const float3 pos = GetClipPos(rayOrigin, rayDir);
	g_rwCubeDepth[DTid] = pos.z;
	tMax = GetTMax(pos, rayOrigin, rayDir, tMax);
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
	min16float step = stepScale;
	float prevDensity = 0.0;
	for (uint i = 0; i < g_numSamples; ++i)
	{
		const float3 pos = rayOrigin + rayDir * t;
		const float3 uvw = LocalToTex3DSpace(pos);

		// Get a sample
		//float mip = max(0.5 - transm, 0.0) * 2.0;
		//const float mip1 = WaveReadLaneAt(mip, couple);
		//mip = min(mip, mip1);
		//min16float4 color = GetSample(uvw, mip);
		min16float4 color = GetSample(uvw);
		min16float newStep = stepScale;
		float dDensity = 1.0;

		// Skip empty space
		if (color.w > ZERO_THRESHOLD)
		{
#ifdef _POINT_LIGHT_
			// Point light direction in texture space
			const float3 lightDir = normalize(localSpaceLightPt - pos);
#endif

			const float3 light = GetLight(pos, lightDir, shCoeffs); // Sample light

			// Update step
			dDensity = color.w - prevDensity;
			const min16float opacity = saturate(color.w * step);
			newStep = GetStep(dDensity, transm, opacity, stepScale);
			step = (step + newStep) * 0.5;
			prevDensity = color.w;

			// Accumulate color
			const min16float tansl = GetTranslucency(color.w, step);
			color.w = saturate(color.w * step);
#ifdef _PRE_MULTIPLIED_
			color.xyz = GetPremultiplied(color.xyz, step);
#else
			//color.xyz *= color.w;
			color.xyz *= color.w;
#endif
			color.xyz *= transm;

			//scatter += color.xyz;
			scatter += color.xyz * min16float3(light);

			// Attenuate ray-throughput
			transm *= 1.0 - tansl;
			if (transm < ZERO_THRESHOLD) break;
		}

		// Update position along ray
		step = newStep;
		t += step;
		if (t > tMax) break;
	}

	scatter.xyz /= 2.0 * PI;

	//scatter = eyeIdx ? min16float3(0.5 * scatter.x, scatter.yz) : min16float3(scatter.x, 0.5 * scatter.yz);
	g_rwCubeMap[DTid] = float4(scatter, 1.0 - transm);
}
