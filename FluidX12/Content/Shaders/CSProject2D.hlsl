//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

static const float g_restDensity = 0.8;

//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------
Texture3D<float3>	g_txVelocity;

RWTexture3D<float3>	g_rwVelocity;
globallycoherent RWTexture3D<float> g_rwIncompress;

//--------------------------------------------------------------------------------------
// Compute 2D divergence
//--------------------------------------------------------------------------------------
float GetDivergence(Texture3D<float3> txU, uint2 cell, uint4 cellRange)
{
	const float l = txU[uint3(cellRange.x, cell.y, 0)].x;
	const float r = txU[uint3(cellRange.z, cell.y, 0)].x;
	const float t = txU[uint3(cell.x, cellRange.y, 0)].y;
	const float b = txU[uint3(cell.x, cellRange.w, 0)].y;

	// Compute the divergence using central differences
	return 0.5 * (r - l + b - t);
}

//--------------------------------------------------------------------------------------
// 2D Poisson solver
//--------------------------------------------------------------------------------------
void Poisson(RWTexture3D<float> rwX, float b, uint2 cell, uint4 cellRange)
{
	for (uint i = 0; i < 48; ++i)
	{
		// Jacobi/Gauss-Seidel iterations
		const float x0 = rwX[uint3(cellRange.x, cell.y, 0)];
		const float xL = rwX[uint3(cellRange.x, cell.y, 0)];
		const float xR = rwX[uint3(cellRange.z, cell.y, 0)];
		const float xT = rwX[uint3(cell.x, cellRange.y, 0)];
		const float xB = rwX[uint3(cell.x, cellRange.w, 0)];

		const float x = (xL + xR + xT + xB - b) / 4.0;
		rwX[uint3(cell, 0)] = x;
		DeviceMemoryBarrier();

		if (abs(x - x0) < 0.001) break;
	}
}

//--------------------------------------------------------------------------------------
// 2D Projection
//--------------------------------------------------------------------------------------
void Project(RWTexture3D<float> rwQ, inout float3 u, uint2 cell, uint4 cellRange)
{
	const float qL = rwQ[uint3(cellRange.x, cell.y, 0)];
	const float qR = rwQ[uint3(cellRange.z, cell.y, 0)];
	const float qT = rwQ[uint3(cell.x, cellRange.y, 0)];
	const float qB = rwQ[uint3(cell.x, cellRange.w, 0)];

	// Project the velocity onto its divergence-free component
	// Compute the gradient using central differences
	u.xy -= 0.5 * float2(qR - qL, qB - qT) / g_restDensity;
}

//--------------------------------------------------------------------------------------
// Compute shader of projection
//--------------------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint3 dim;
	g_txVelocity.GetDimensions(dim.x, dim.y, dim.z);

	uint4 cellRange;
	cellRange.xy = max(DTid.xy, 1) - 1;
	cellRange.zw = min(DTid.xy + 1, dim.xy - 1);

	// Fetch velocity field
	const float b = GetDivergence(g_txVelocity, DTid.xy, cellRange);
	float3 u = g_txVelocity[DTid];

	// Boundary process
	int3 offset;
	offset.x = DTid.x + 1 >= dim.x ? -1 : (DTid.x <= 1 ? 1 : 0);
	offset.y = DTid.y + 1 >= dim.y ? -1 : (DTid.y <= 1 ? 1 : 0);
	offset.z = 0;
	if (any(offset.xy)) u = -g_txVelocity[DTid + offset];

	// Poisson solver
	Poisson(g_rwIncompress, b, DTid.xy, cellRange);

	// Projection
	Project(g_rwIncompress, u, DTid.xy, cellRange);

	g_rwVelocity[DTid] = u;
}
