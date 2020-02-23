//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define EMISSION_RADIUS (1.0 / 56.0)

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
cbuffer cbPerFrame
{
	float g_timeStep;
};

static const float3 g_emissionPt = { 0.5, 0.93, 0.5 };
static const float g_emissionR_sq = EMISSION_RADIUS * EMISSION_RADIUS;
static const float3 g_emissionForce = float3(0.0, -80.0, 0.0);
static const float4 g_emissionDye = float4(100.0, 40.0, 0.0, 64.0);
static const float g_dissipation = 0.1;

//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------
RWTexture3D<float3> g_rwVelocity;
RWTexture3D<float4>	g_rwDye;

Texture3D<float3>	g_txVelocity;
Texture3D			g_txDye;

//--------------------------------------------------------------------------------------
// Sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpLinear;

//--------------------------------------------------------------------------------------
// Compute shader of advection
//--------------------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	// Fetch velocity field
	float3 dim;
	g_txVelocity.GetDimensions(dim.x, dim.y, dim.z);
	float3 u = g_txVelocity[DTid];

	// Advections
	float3 pos = (DTid + 0.5) / dim;
	const float3 adv = pos - u * g_timeStep;
	u = g_txVelocity.SampleLevel(g_smpLinear, adv, 0.0);
	float4 dye = g_txDye.SampleLevel(g_smpLinear, adv, 0.0);

	// Impulse
	const float3 disp = pos - g_emissionPt;
	const float basis = exp(-dot(disp, disp) / g_emissionR_sq);
	u += g_emissionForce * g_timeStep * basis;
	dye += g_emissionDye * g_timeStep * basis;

	// Output
	g_rwVelocity[DTid] = u;
	g_rwDye[DTid] = max(dye - dye * g_dissipation * g_timeStep, 0.0);
}
