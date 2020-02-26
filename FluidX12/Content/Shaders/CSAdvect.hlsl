//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Impulse.hlsli"

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
static const float3	g_extForce = float3(0.0, -48.0, 0.0);
static const float	g_forceScl3D = 4.0;
static const float	g_vortScl = 200.0;
static const float	g_dissipation = 0.1;

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
// Grid space to simulation space
//--------------------------------------------------------------------------------------
float3 GridToSimulationSpace(uint3 index, float3 gridSize)
{
	return (index + 0.5) / gridSize;
}

//--------------------------------------------------------------------------------------
// Grid space to simulation space
//--------------------------------------------------------------------------------------
float3 SimulationToTextureSpace(float3 pos, float3 gridSize)
{
	return pos;
}

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
	const float timeStep = g_timeStep;

	// Fetch velocity field
	float3 gridSize;
	g_txVelocity.GetDimensions(gridSize.x, gridSize.y, gridSize.z);
	const float3 u = g_txVelocity[DTid];

	// Advections
	const float3 pos = GridToSimulationSpace(DTid, gridSize);
	const float3 adv = SimulationToTextureSpace(pos - u * timeStep, gridSize);
	float3 w = g_txVelocity.SampleLevel(g_smpLinear, adv, 0.0);
#if ADVECT_COLOR
	float4 color = g_txColor.SampleLevel(g_smpLinear, adv, 0.0);
#endif

	// Impulse
	const float3 disp = pos - g_impulsePos;
	float basis = Gaussian(disp, g_impulseR);
	if (basis >= exp(-4.0))
	{
		//basis = sqrt(basis) * 0.4;
		const float3 vortForce = float3(-disp.z, 0.0, disp.x) * g_vortScl;
		float3 extForce = g_extForce * basis;
		extForce = gridSize.z > 1 ? extForce * g_forceScl3D + vortForce : extForce;
		w += extForce * timeStep;
#if ADVECT_COLOR
		color += g_impulse * timeStep * basis;
#endif
	}

	// Output
	g_rwVelocity[DTid] = w;
#if ADVECT_COLOR
	g_rwColor[DTid] = color * max(1.0 - g_dissipation * timeStep, 0.0);
#endif
}
