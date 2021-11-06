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
// Compute end point of the ray on the cube surface
//--------------------------------------------------------------------------------------
uint ComputeRayHit(inout float3 pos, float3 rayDir)
{
	//float U = INF;
	float U = FLT_MAX;
	uint hitPlane = 0xffffffff;

	[unroll]
	for (uint i = 0; i < 3; ++i)
	{
		const float u = (sign(rayDir[i]) - pos[i]) / rayDir[i];
		if (u < 0.0) continue;

		const uint j = (i + 1) % 3, k = (i + 2) % 3;
		if (abs(rayDir[j] * u + pos[j]) > 1.0) continue;
		if (abs(rayDir[k] * u + pos[k]) > 1.0) continue;
		if (u < U)
		{
			U = u;
			hitPlane = i;
		}
	}

	//pos = clamp(rayDir * U + pos, -1.0, 1.0);
	pos = rayDir * U + pos;

	return hitPlane;
}

//--------------------------------------------------------------------------------------
// Compute texcoord
//--------------------------------------------------------------------------------------
float3 ComputeCubeTexcoord(float3 pos, uint hitPlane)
{
	float3 uvw;

	switch (hitPlane)
	{
	case 0: // X
		uvw.x = -pos.x * pos.z;
		uvw.y = pos.y;
		uvw.z = pos.x < 0.0 ? hitPlane * 2 + 1 : hitPlane * 2;
		break;
	case 1: // Y
		uvw.x = pos.x;
		uvw.y = -pos.y * pos.z;
		uvw.z = pos.y < 0.0 ? hitPlane * 2 + 1 : hitPlane * 2;
		break;
	case 2: // Z
		uvw.x = pos.z * pos.x;
		uvw.y = pos.y;
		uvw.z = pos.z < 0.0 ? hitPlane * 2 + 1 : hitPlane * 2;
		break;
	default:
		uvw = 0.0;
		break;
	}
	uvw.xy = uvw.xy * 0.5 + 0.5;
	uvw.y = 1.0 - uvw.y;

	return uvw;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
min16float4 main(PSIn input) : SV_TARGET
{
	float3 pos = TexcoordToLocalPos(input.UV);	// The point on the near plane
	const float3 localSpaceEyePt = mul(float4(g_eyePt, 1.0), g_worldI);
	const float3 rayDir = normalize(pos - localSpaceEyePt);

	const uint hitPlane = ComputeRayHit(pos, rayDir);
	if (hitPlane > 2) discard;

	const float3 uvw = ComputeCubeTexcoord(pos, hitPlane);

	return CubeCast(input.Pos.xy, uvw, pos, rayDir);
}
