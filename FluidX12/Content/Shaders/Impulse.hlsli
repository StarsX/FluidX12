//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
cbuffer cbPerFrame
{
	float	g_timeStep;
	uint	g_baseSeed;
};

static const float3	g_impulsePos = { 0.5, 0.1, 0.5 };
static const float	g_impulseR = 1.0 / 16.0;
static const float3	g_impulseColor = { 0.2, 0.4, 1.0 };
static const float g_impulseDensity = 40.0;
static const float4	g_impulse = float4(g_impulseColor, 1.0) * g_impulseDensity;
