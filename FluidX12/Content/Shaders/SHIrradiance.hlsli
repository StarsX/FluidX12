//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define PI 3.1415926535897

//--------------------------------------------------------------------------------------
// Spherical harmonics
//--------------------------------------------------------------------------------------
float3 EvaluateSHIrradiance(StructuredBuffer<float3> roSHCoeffs, float3 norm)
{
	const float c1 = 0.42904276540489171563379376569857;	// 4 * A2.Y22 = 1/4 * sqrt(15.PI)
	const float c2 = 0.51166335397324424423977581244463;	// 0.5 * A1.Y10 = 1/2 * sqrt(PI/3)
	const float c3 = 0.24770795610037568833406429782001;	// A2.Y20 = 1/16 * sqrt(5.PI)
	const float c4 = 0.88622692545275801364908374167057;	// A0.Y00 = 1/2 * sqrt(PI)

	const float x = -norm.x;
	const float y = -norm.y;
	const float z = norm.z;

	const float3 irradiance = max(0.0,
		(c1 * (x * x - y * y)) * roSHCoeffs[8]													// c1.L22.(x^2 - y^2)
		+ (c3 * (3.0 * z * z - 1.0)) * roSHCoeffs[6]											// c3.L20.(3.z^2 - 1)
		+ c4 * roSHCoeffs[0]																	// c4.L00 
		+ 2.0 * c1 * (roSHCoeffs[4] * x * y + roSHCoeffs[7] * x * z + roSHCoeffs[5] * y * z)	// 2.c1.(L2-2.xy + L21.xz + L2-1.yz)
		+ 2.0 * c2 * (roSHCoeffs[3] * x + roSHCoeffs[1] * y + roSHCoeffs[2] * z));				// 2.c2.(L11.x + L1-1.y + L10.z)

	return irradiance / PI;
}
