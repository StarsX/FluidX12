//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Impulse.hlsli"

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
static const float3	g_extforce = float3(0.0, -80.0, 0.0);
#if ADVECT_COLOR
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
// Gaussian function
//--------------------------------------------------------------------------------------
float Gaussian(float3 disp, float r)
{
	return exp(-dot(disp, disp) / (r * r));
}

//--------------------------------------------------------------------------------------
// Compute shader of advection
//--------------------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	const float timeStep = g_timeStep;

	// Fetch velocity field
	float3 dim;
	g_txVelocity.GetDimensions(dim.x, dim.y, dim.z);
	const float3 u = g_txVelocity[DTid];

	// Advections
	const float3 texel = 1.0 / dim;
	const float3 pos = (DTid + 0.5) * texel;
	const float3 adv = pos - u * timeStep;
	float3 w = g_txVelocity.SampleLevel(g_smpLinear, adv, 0.0);
#if ADVECT_COLOR
	float4 color = g_txColor.SampleLevel(g_smpLinear, adv, 0.0);
#endif

	// Impulse
	const float basis = Gaussian(pos - g_impulsePos, g_impulseR);
	w += g_extforce * timeStep * basis;
#if ADVECT_COLOR
	color += g_impulse * timeStep * basis;
#endif

	// Output
	g_rwVelocity[DTid] = w;
#if ADVECT_COLOR
	g_rwColor[DTid] = max(color - color * g_dissipation * timeStep, 0.0);
#endif
}
