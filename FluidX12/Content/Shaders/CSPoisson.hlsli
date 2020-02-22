//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Poisson solver
//--------------------------------------------------------------------------------------
void Poisson(RWTexture3D<float> rwX, float b, uint3 cell, uint3 cells[N])
{
	for (uint k = 0; k < ITER; ++k)
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
