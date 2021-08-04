//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define L 0
#define R 1
#define U 2
#define D 3
#define N 4

#define ITER 64

#include "Simulation.hlsli"
#include "CSPoisson.hlsli"

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
cbuffer cbPerFrame
{
	float g_timeStep;
};

static const float g_density = 1.0;

//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------
Texture3D<float3>	g_txVelocity;

RWTexture3D<float3>	g_rwVelocity;
globallycoherent RWTexture3D<float> g_rwIncompress;

//--------------------------------------------------------------------------------------
// Compute divergence
//--------------------------------------------------------------------------------------
float GetDivergence(Texture3D<float3> txU, uint3 cells[N])
{
	const float fL = txU[cells[L]].x;
	const float fR = txU[cells[R]].x;
	const float fU = txU[cells[U]].y;
	const float fD = txU[cells[D]].y;

	// Compute the divergence using central differences
	return 0.5 * ((fR - fL) + (fD - fU));
}

//--------------------------------------------------------------------------------------
// Projection
//--------------------------------------------------------------------------------------
void Project(RWTexture3D<float> rwQ, inout float3 u, uint3 cells[N])
{
	float q[N];
	[unroll] for (uint i = 0; i < N; ++i) q[i] = rwQ[cells[i]];

	// Project the velocity onto its divergence-free component
	// Compute the gradient using central differences
	u.xy -= 0.5 * float2(q[R] - q[L], q[D] - q[U]) / g_density;
}

//--------------------------------------------------------------------------------------
// Compute shader of projection
//--------------------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint3 gridSize;
	g_txVelocity.GetDimensions(gridSize.x, gridSize.y, gridSize.z);

	// Neighbor cells
	uint3 cells[N];
	const uint2 cellMin = max(DTid.xy, 1) - 1;
	const uint2 cellMax = min(DTid.xy + 1, gridSize.xy - 1);
	cells[L] = uint3(cellMin.x, DTid.y, 0);
	cells[R] = uint3(cellMax.x, DTid.y, 0);
	cells[U] = uint3(DTid.x, cellMin.y, 0);
	cells[D] = uint3(DTid.x, cellMax.y, 0);

	// Fetch velocity field
	float3 u = g_txVelocity[DTid];

	if (g_timeStep > 0.0)
	{
		// Compute divergence
		const float b = GetDivergence(g_txVelocity, cells);

		// Boundary process
#if 0
		const int2 offset = DTid + 2 >= gridSize ? -1 : (DTid < 2 ? 1 : 0);
		if (any(offset.xy)) u = -g_txVelocity[DTid + int3(offset, 0)];
#endif

		// Poisson solver
		Poisson(g_rwIncompress, b, DTid, cells);

		// Projection
		Project(g_rwIncompress, u, cells);

		// Boundary process
		float3 pos = GridToSimulationSpace(DTid, gridSize);
		pos.xy = pos.xy * 2.0 - 1.0;
		u *= u * pos > 0.0 ? clamp((0.97 - abs(pos)) / 0.03, -1.0, 1.0) : 1.0;
	}

	g_rwVelocity[DTid] = u;
}
