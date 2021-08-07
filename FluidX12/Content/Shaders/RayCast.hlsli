//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define _TEXCOORD_INVERT_Y_
#define _PRE_MULTIPLIED_

#define	INF				asfloat(0x7f800000)
#define	FLT_MAX			3.402823466e+38
#define DENSITY_SCALE	16.0

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
cbuffer cbPerFrame
{
	float3 g_eyePt;
	float4x3 g_lightMapWorld;
	float3 g_lightPt;
	float4 g_lightColor;
	float4 g_ambient;
};

cbuffer cbPerObject
{
	matrix g_worldViewProjI;
	float4x3 g_worldI;
	float4x3 g_localToLight;
};

//--------------------------------------------------------------------------------------
// Texture sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpLinear;
