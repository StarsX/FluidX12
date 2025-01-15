//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Simulation.hlsli"
#include "Impulse.hlsli"

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
static const float3	g_extForce = float3(0.0, 48.0, 0.0);
static const float	g_forceScl3D = 4.0;
static const float	g_vortScl = 200.0;
static const float	g_dissipation = 0.2;

//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------
RWTexture3D<float3> g_rwVelocity;
RWTexture3D<float4>	g_rwColor;

Texture3D<float3>	g_txVelocity;
Texture3D			g_txColor;

//--------------------------------------------------------------------------------------
// Sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpLinear;

//--------------------------------------------------------------------------------------
// Gaussian function
//--------------------------------------------------------------------------------------
float Gaussian(float3 disp, float r)
{
	return exp(-4.0 * dot(disp, disp) / (r * r));
}

//--------------------------------------------------------------------------------------
// Compute shader of advection
//--------------------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	// Fetch velocity field
	float3 gridSize;
	g_txVelocity.GetDimensions(gridSize.x, gridSize.y, gridSize.z);
	float3 u = g_txVelocity[DTid];

	// Advections
	const float timeStep = g_timeStep;
	const float3 pos = GridToSimulationSpace(DTid, gridSize);
	const float3 adv = SimulationToTextureSpace(pos - u * timeStep, gridSize);
	u = g_txVelocity.SampleLevel(g_smpLinear, adv, 0.0);
	float4 color = g_txColor.SampleLevel(g_smpLinear, adv, 0.0);

	// Impulse
	const float3 disp = pos - g_impulsePos;
	const float impulseR = gridSize.z > 1.0 ? g_impulseR : g_impulseR * 0.5;
	float basis = Gaussian(disp, impulseR);
	if (basis >= exp(-4.0))
	{
		//basis = sqrt(basis) * 0.4;
		const float3 vortForce = float3(-disp.z, 0.0, disp.x) * g_vortScl;
		float3 extForce = g_extForce * basis;
		extForce = gridSize.z > 1 ? extForce * g_forceScl3D + vortForce : extForce;
		u += extForce * timeStep;
		color = saturate(color + g_impulse * timeStep * basis);
	}

#ifndef _PRE_MULTIPLIED_
	color.xyz = color.w > 0.0 ? color.xyz / color.w : color.xyz;
#endif

	const float atten = max(1.0 - g_dissipation * timeStep, 0.0);

	// Output
	g_rwVelocity[DTid] = u * atten;
	g_rwColor[DTid] = color * atten;
}
