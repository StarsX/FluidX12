//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#define _TEXCOORD_INVERT_Y_
#define _PRE_MULTIPLIED_
#define _HAS_LIGHT_PROBE_

#define	INF				asfloat(0x7f800000)
#define	FLT_MAX			3.402823466e+38
#define DENSITY_SCALE	90.0

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
cbuffer cbPerObject
{
	float4x4 g_worldViewProjI;
	float4x4 g_worldViewProj;
	float4x3 g_worldI;
	float4x3 g_world;
};

cbuffer cbPerFrame
{
	float3 g_eyePt;
	//float4x4 g_shadowViewProj;
	float3 g_lightPt;
	float4 g_lightColor;
	float4 g_ambient;
};

//--------------------------------------------------------------------------------------
// Texture sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpLinear;
