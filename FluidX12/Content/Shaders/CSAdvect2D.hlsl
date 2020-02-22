//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define EMISSION_RADIUS (1.0 / 64.0)

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
cbuffer cbPerFrame
{
	float g_timeStep;
};

static const float2 g_emissionPt = { 0.5, 0.9 };
static const float g_emissionR_sq = EMISSION_RADIUS * EMISSION_RADIUS;
static const float3 g_emissionForce = float3(0.0, -48.0, 0.0);
static const float4 g_emissionDye = float4(100.0, 40.0, 0.0, 80.0);
static const float g_dissipation = 1.0;

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
	const float2 texel = 1.0 / dim.xy;
	float3 pos = { (DTid.xy + 0.5) * texel, 0.5 };
	const float3 adv = pos - u * g_timeStep;
	u = g_txVelocity.SampleLevel(g_smpLinear, adv, 0.0);
	float4 dye = g_txDye.SampleLevel(g_smpLinear, adv, 0.0);

	// Impulse
	const float2 disp = pos.xy - g_emissionPt;
	const float basis = exp(-dot(disp, disp) / g_emissionR_sq);
	u += g_emissionForce * g_timeStep * basis;
	dye += g_emissionDye * g_timeStep * basis;

	// Output
	g_rwVelocity[DTid] = u;
	g_rwDye[DTid] = max(dye - dye * g_dissipation * g_timeStep, 0.0);
}
