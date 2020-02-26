//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define L 0
#define R 1
#define U 2
#define D 3
#define F 4
#define B 5
#define N 6

#define ITER 64

#include "CSPoisson.hlsli"

static const float g_density = 0.48;

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
	const float fF = txU[cells[F]].z;
	const float fB = txU[cells[B]].z;

	// Compute the divergence using central differences
	return 0.5 * ((fR - fL) + (fD - fU) + (fB - fF));
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
	u -= 0.5 * float3(q[R] - q[L], q[D] - q[U], q[B] - q[F]) / g_density;
}

//--------------------------------------------------------------------------------------
// Compute shader of projection
//--------------------------------------------------------------------------------------
[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint3 gridSize;
	g_txVelocity.GetDimensions(gridSize.x, gridSize.y, gridSize.z);

	// Neighbor cells
	uint3 cells[N];
	const uint3 cellMin = max(DTid, 1) - 1;
	const uint3 cellMax = min(DTid + 1, gridSize - 1);
	cells[L] = uint3(cellMin.x, DTid.yz);
	cells[R] = uint3(cellMax.x, DTid.yz);
	cells[U] = uint3(DTid.x, cellMin.y, DTid.z);
	cells[D] = uint3(DTid.x, cellMax.y, DTid.z);
	cells[F] = uint3(DTid.xy, cellMin.z);
	cells[B] = uint3(DTid.xy, cellMax.z);

	// Fetch velocity field
	const float b = GetDivergence(g_txVelocity, cells);
	float3 u = g_txVelocity[DTid];

	// Boundary process
	int3 offset;
	offset.x = DTid.x + 2 >= gridSize.x ? -1 : (DTid.x < 2 ? 1 : 0);
	offset.y = DTid.y + 2 >= gridSize.y ? -1 : (DTid.y < 2 ? 1 : 0);
	offset.z = DTid.z + 2 >= gridSize.z ? -1 : (DTid.z < 2 ? 1 : 0);
	if (any(offset)) u = -g_txVelocity[DTid + offset];

	// Poisson solver
	Poisson(g_rwIncompress, b, DTid, cells);

	// Projection
	Project(g_rwIncompress, u, cells);

	g_rwVelocity[DTid] = u;
}
