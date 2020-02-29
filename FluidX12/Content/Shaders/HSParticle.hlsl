//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

// Input/output control point
struct HSInOut
{
	float4 Pos		: POSITION;
	float4 Color	: COLOR;
};

// Output patch constant data.
struct HSConstDataOut
{
	float EdgeTessFactor[4]		: SV_TessFactor;
	float InsideTessFactor[2]	: SV_InsideTessFactor;
};

// Patch Constant Function
HSConstDataOut CalcHSPatchConstants(InputPatch<HSInOut, 1> ip)
{
	HSConstDataOut output;

	// Output tess factors
	const float tessFactor = 1.0;
	output.EdgeTessFactor[0] = output.EdgeTessFactor[2] = tessFactor;
	output.EdgeTessFactor[1] = output.EdgeTessFactor[3] = tessFactor;
	output.InsideTessFactor[0] = output.InsideTessFactor[1] = tessFactor;

	return output;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(1)]
[patchconstantfunc("CalcHSPatchConstants")]
HSInOut main(InputPatch<HSInOut, 1> ip,
	uint i : SV_OutputControlPointID)
{
	// Pass-through
	HSInOut output = ip[i];

	return output;
}
