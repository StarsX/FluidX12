//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define IMPULSE_RADIUS (1.0 / 56.0)

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
cbuffer cbPerFrame
{
	float g_timeStep;
};

static const float3	g_impulsePos = { 0.5, 0.93, 0.5 };
static const float	g_impulseR_sq = IMPULSE_RADIUS * IMPULSE_RADIUS;
static const float3	g_extforce = float3(0.0, -80.0, 0.0);
#if ADVECT_COLOR
static const float4	g_impulse = float4(100.0, 40.0, 0.0, 64.0);
static const float	g_dissipation = 0.1;
#endif

//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------
RWTexture3D<float3> g_rwVelocity;
Texture3D<float3>	g_txVelocity;

#if ADVECT_COLOR
RWTexture3D<float4>	g_rwColor;
Texture3D			g_txColor;
#endif

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
#if ADVECT_COLOR
	float4 color = g_txColor.SampleLevel(g_smpLinear, adv, 0.0);
#endif

	// Impulse
	const float3 disp = pos - g_impulsePos;
	const float basis = exp(-dot(disp, disp) / g_impulseR_sq);
	u += g_extforce * g_timeStep * basis;
#if ADVECT_COLOR
	color += g_impulse * g_timeStep * basis;
#endif

	// Output
	g_rwVelocity[DTid] = u;
#if ADVECT_COLOR
	g_rwColor[DTid] = max(color - color * g_dissipation * g_timeStep, 0.0);
#endif
}
