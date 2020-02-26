//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

struct PSIn
{
	float4 Pos		: SV_POSITION;
	float4 Color	: COLOR;
};

float4 main(PSIn input) : SV_TARGET
{
	return float4(sqrt(input.Color.xyz), saturate(input.Color.w));
}
