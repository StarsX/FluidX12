//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

struct PSIn
{
	float4 Pos		: SV_POSITION;
	float4 Color	: COLOR;
	float2 Tex		: TEXCOORD;
};

float4 main(PSIn input) : SV_TARGET
{
	const float2 r = input.Tex - 0.5;
	if (dot(r, r) > 0.25) discard;

	return float4(sqrt(input.Color.xyz), saturate(input.Color.w));
}
