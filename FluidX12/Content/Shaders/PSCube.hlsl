//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#include "PSCube.hlsli"

//--------------------------------------------------------------------------------------
// Structure
//--------------------------------------------------------------------------------------
struct PSIn
{
	float4 Pos	: SV_POSITION;
	float3 UVW	: TEXCOORD;
	float3 LPt	: POSLOCAL;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
min16float4 main(PSIn input) : SV_TARGET
{
	const float3 localSpaceEyePt = mul(float4(g_eyePt, 1.0), g_worldI);
	const float3 rayDir = input.LPt - localSpaceEyePt;

	const min16float4 result = CubeCast(input.Pos.xy, input.UVW, input.LPt, rayDir);
	if (result.w <= 0.0) discard;

	return result;
}
