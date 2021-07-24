//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define _TEXCOORD_INVERT_Y_
#define _GAMMA_

#define DENSITY_SCALE 24.0

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
cbuffer cbPerObject
{
	matrix g_worldViewProjI;
	float4x3 g_worldI;
	float4x3 g_lightMapWorld;
	float4 g_eyePos;
	float4 g_lightPos;
	float4 g_lightColor;
	float4 g_ambient;
};

//--------------------------------------------------------------------------------------
// Texture sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpLinear;
