//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define PI 3.1415926535897

//--------------------------------------------------------------------------------------
// Structure
//--------------------------------------------------------------------------------------
struct PSIn
{
	float4 Pos		: SV_POSITION;
	float4 Color	: COLOR;
	float3 Nrm		: NORMAL;
	float2 Tex		: TEXCOORD;
};

float Wyvill(float x)
{
	x = 1.0 - x;

	return x * x * x;
}

//--------------------------------------------------------------------------------------
// Pixel shader of particle rendering
//--------------------------------------------------------------------------------------
float4 main(PSIn input) : SV_TARGET
{
	// Clip shape
	const float2 r = input.Tex - 0.5;
	const float r_sq = dot(r, r);
	if (r_sq > 0.25) discard;

	float4 color = input.Color;
	const float lightAmt = saturate(dot(input.Nrm, normalize(float3(1.0, 1.0, -1.0))));
	color.xyz *= (lightAmt + 0.16);
	color.xyz *= color.w;

	//color.w = sqrt(color.w);
	color.w *= Wyvill(4.0 * r_sq) * 2.0;

	const float range = saturate((input.Pos.z - 1.5) / -2.5);
	const float rangeInv = 1.0 - range;
	color.w *= range * rangeInv * rangeInv;

	return float4(color.xyz / (color.xyz + 0.5), saturate(color.w));
}
