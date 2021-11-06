//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Simulation.hlsli"

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct DSIn
{
	float3 Pos : POSITION;
};

struct DSOut
{
	float4 Pos		: SV_POSITION;
	float4 Color	: COLOR;
	float3 Nrm		: NORMAL;
	float2 Tex		: TEXCOORD;
};

// Output patch constant data.
struct HSConstDataOut
{
	float EdgeTessFactor[4]		: SV_TessFactor;
	float InsideTessFactor[2]	: SV_InsideTessFactor;
};

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
cbuffer cbPerObject : register (b1)
{
	float4x3 g_worldView;
	float4x3 g_worldViewI;
	float4x4 g_proj;
};

static const float g_particleRadius = 0.4;

//--------------------------------------------------------------------------------------
// Buffer and textures
//--------------------------------------------------------------------------------------
Texture3D g_txColor;

//--------------------------------------------------------------------------------------
// Sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpLinear;

//--------------------------------------------------------------------------------------
// Simulation space to object space
//--------------------------------------------------------------------------------------
float3 ObjectToSimulationSpace(float3 pos, bool is3D)
{
	pos.y = -pos.y;

	return pos * 0.5 + 0.5;
}

//--------------------------------------------------------------------------------------
// Get density gradient
//--------------------------------------------------------------------------------------
float3 GetDensityGradient(float3 tex, float3 gridSize)
{
	const float3 halfTexel = 0.5 / gridSize;
	const float rhoL = g_txColor.SampleLevel(g_smpLinear, tex + float3(-halfTexel.x, 0.0.xx), 0.0).w;
	const float rhoR = g_txColor.SampleLevel(g_smpLinear, tex + float3(halfTexel.x, 0.0.xx), 0.0).w;
	const float rhoU = g_txColor.SampleLevel(g_smpLinear, tex + float3(0.0, -halfTexel.y, 0.0), 0.0).w;
	const float rhoD = g_txColor.SampleLevel(g_smpLinear, tex + float3(0.0, halfTexel.y, 0.0), 0.0).w;
	const float rhoF = g_txColor.SampleLevel(g_smpLinear, tex + float3(0.0.xx, -halfTexel.z), 0.0).w;
	const float rhoB = g_txColor.SampleLevel(g_smpLinear, tex + float3(0.0.xx, halfTexel.z), 0.0).w;

	return float3(rhoR - rhoL, rhoD - rhoU, rhoB - rhoF);
}

//--------------------------------------------------------------------------------------
// Get normal vector
//--------------------------------------------------------------------------------------
float3 GetNormal(float3 tex, float3 gridSize)
{
	float3 gradient = GetDensityGradient(tex, gridSize);
	gradient.z = gridSize.z > 1.0 ? gradient.z : -1.0;

	return normalize(-gradient);
}

[domain("quad")]
DSOut main(HSConstDataOut input,
	float2 domain : SV_DomainLocation,
	const OutputPatch<DSIn, 1> patch)
{
	DSOut output;

	// Get grid size
	float3 gridSize;
	g_txColor.GetDimensions(gridSize.x, gridSize.y, gridSize.z);

	// Caculate position offset
	float2 offset = domain * 2.0 - 1.0;
	offset.y = -offset.y;
	offset *= g_particleRadius;

	// Caculate vertex position
	float4 pos = float4(patch[0].Pos, 1.0);
	pos.xy += offset;

	// Sampling and calculate color and normal
	const bool is3D = gridSize.z > 1.0;
	const float3 oPos = mul(pos, g_worldViewI).xyz;
	const float3 sPos = ObjectToSimulationSpace(oPos, is3D);
	const float3 tex = SimulationToTextureSpace(sPos, is3D);
	output.Color = g_txColor.SampleLevel(g_smpLinear, tex, 0.0);
	output.Nrm = GetNormal(tex, gridSize);

	output.Pos = mul(pos, g_proj);
	output.Tex = domain;

	return output;
}
