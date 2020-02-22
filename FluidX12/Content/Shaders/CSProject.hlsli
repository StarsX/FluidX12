//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Poisson solver
//--------------------------------------------------------------------------------------
void Poisson(RWTexture3D<float> rwX, float b, uint3 cell, uint3 cells[N])
{
	for (uint k = 0; k < 48; ++k)
	{
		// Jacobi/Gauss-Seidel iterations
		float q[N];
		[unroll] for (uint i = 0; i < N; ++i) q[i] = rwX[cells[i]];
		const float x0 = rwX[cell];

		float x = -b;
		[unroll] for (i = 0; i < N; ++i) x += q[i];
		x /= N;

		rwX[cell] = x;
		DeviceMemoryBarrier();

		if (abs(x - x0) < 0.001) break;
	}
}

//--------------------------------------------------------------------------------------
// Projection
//--------------------------------------------------------------------------------------
void Project(RWTexture3D<float> rwQ, inout float3 u, uint3 cells[N], float rho)
{
	float q[N];
	[unroll] for (uint i = 0; i < N; ++i) q[i] = rwQ[cells[i]];

	// Project the velocity onto its divergence-free component
	// Compute the gradient using central differences
	u.xy -= 0.5 * float2(q[R] - q[L], q[D] - q[U]) / rho;
}
