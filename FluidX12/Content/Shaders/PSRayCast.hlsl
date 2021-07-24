//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
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
	const float3 localSpaceEyePt = mul(g_eyePos, g_worldI);

	const float3 rayDir = normalize(rayOrigin - localSpaceEyePt);
	if (!ComputeRayOrigin(rayOrigin, rayDir)) discard;

#ifdef _HAS_DEPTH_MAP_
	// Calculate occluded end point
	const float3 pos = GetClipPos(input.Pos.xy, input.UV);
	const float tMax = GetTMax(pos, rayOrigin, rayDir);
#endif

#ifdef _POINT_LIGHT_
	const float3 localSpaceLightPt = mul(g_lightPos, g_worldI);
#else
	const float3 localSpaceLightPt = mul(g_lightPos.xyz, (float3x3)g_worldI);
	const float3 lightStep = normalize(localSpaceLightPt) * g_lightStepScale;
#endif

	// Transmittance
	min16float transm = 1.0;

	// In-scattered radiance
	min16float3 scatter = 0.0;

	float t = 0.0;
	for (uint i = 0; i < NUM_SAMPLES; ++i)
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
			const float3 lightStep = normalize(g_localSpaceLightPt - pos) * g_lightStepScale;
#endif
			// Sample light
			const float3 light = GetLight(pos, lightStep);
			
			// Accumulate color
			color.w = GetOpacity(color.w, g_stepScale);
			color.xyz *= transm * color.w;

			//scatter += color.xyz;
			scatter += min16float3(light) * color.xyz;

			// Attenuate ray-throughput
			transm *= 1.0 - color.w;
			if (transm < ZERO_THRESHOLD) break;
		}

		t += max(1.5 * g_stepScale * t, g_stepScale);
#ifdef _HAS_DEPTH_MAP_
		if (t > tMax) break;
#endif
	}

#ifdef _GAMMA_
	return min16float4(sqrt(scatter), 1.0 - transm);
#else
	return min16float4(scatter, 1.0 - transm);
#endif
}
