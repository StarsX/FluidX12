//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct DSIn
{
	float4 Pos		: POSITION;
	float4 Color	: COLOR;
};

struct DSOut
{
	float4 Pos		: SV_POSITION;
	float4 Color	: COLOR;
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
cbuffer cbPerFrame
{
	matrix g_proj;
};

static const float g_particleRadius = 0.1;

[domain("quad")]
DSOut main(HSConstDataOut input,
	float2 domain : SV_DomainLocation,
	const OutputPatch<DSIn, 1> patch)
{
	DSOut output;

	float2 offset = domain * 2.0 - 1.0;
	offset.y = -offset.y;
	offset *= g_particleRadius;

	float4 pos = patch[0].Pos;
	pos.xy += offset;

	output.Pos = mul(pos, g_proj);
	output.Color = patch[0].Color;
	output.Tex = domain;

	return output;
}
