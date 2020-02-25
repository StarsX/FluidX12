//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Impulse.hlsli"

#define RAND_MAX 0xffff
#define PI 3.1415926535897

//--------------------------------------------------------------------------------------
// Struct
//--------------------------------------------------------------------------------------
struct Particle
{
	float3 Pos;
	float3 Velocity;
	float LifeTime;
};

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
cbuffer cbPerObject
{
	matrix g_worldViewProj;
};

static const float g_fullLife = 1.5;

//--------------------------------------------------------------------------------------
// Buffer and texture
//--------------------------------------------------------------------------------------
RWStructuredBuffer<Particle> g_rwParticles;
Texture3D<float3> g_txVelocity;

//--------------------------------------------------------------------------------------
// Sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpLinear;

//--------------------------------------------------------------------------------------
// Random number generator
//--------------------------------------------------------------------------------------
uint rand(inout uint seed)
{
	// The same implementation of current Windows rand()
	// msvcrt.dll: 77C271D8 mov     ecx, [eax + 14h]
	// msvcrt.dll: 77C271DB imul    ecx, 343FDh
	// msvcrt.dll: 77C271E1 add     ecx, 269EC3h
	// msvcrt.dll: 77C271E7 mov     [eax + 14h], ecx
	// msvcrt.dll: 77C271EA mov     eax, ecx
	// msvcrt.dll: 77C271EC shr     eax, 10h
	// msvcrt.dll: 77C271EF and     eax, 7FFFh
	seed = seed * 0x343fd + 0x269ec3;   // a = 214013, b = 2531011

	return (seed >> 0x10) & RAND_MAX;
}

//--------------------------------------------------------------------------------------
// Random number generator with a range
//--------------------------------------------------------------------------------------
uint rand(inout uint2 seed, uint range)
{
	return (rand(seed.x) | (rand(seed.y) << 16)) % range;
}

//--------------------------------------------------------------------------------------
// Common particle emission
//--------------------------------------------------------------------------------------
void Emit(uint particleId, inout Particle particle)
{
	// Load emitter with a random index
	uint2 seed = { particleId, g_baseSeed };
	const float r = g_impulseR * rand(seed, 1000) / 1000.0f;
	const float t = rand(seed, 1000) / 1000.0f;
	const float x = r * cos(t * 2.0 * PI);
	const float y = r * sin(t * 2.0 * PI);

	// Particle emission
	particle.Pos = g_impulsePos + float3(x, y, 0.0);
	particle.LifeTime = g_fullLife + rand(seed, 1000) / 1000.0f;
}

//--------------------------------------------------------------------------------------
// Particle integration or emission
//--------------------------------------------------------------------------------------
void UpdateParticle(uint particleId, inout Particle particle)
{
	if (particle.LifeTime > 0.0)
	{
		// Integrate and update particle
		particle.Velocity = g_txVelocity.SampleLevel(g_smpLinear, particle.Pos, 0.0);
		particle.Pos += particle.Velocity * g_timeStep;
		particle.LifeTime -= g_timeStep;
	}
	else Emit(particleId, particle);

	g_rwParticles[particleId] = particle;
}

//--------------------------------------------------------------------------------------
// Vertex shader of particle integration or emission
//--------------------------------------------------------------------------------------
float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_rwParticles[ParticleId];

	// Update particle
	UpdateParticle(ParticleId, particle);

	// Calculate world position
	uint3 dim;
	g_txVelocity.GetDimensions(dim.x, dim.y, dim.z);
	float3 pos = particle.Pos * 2.0 - 1.0;
	pos.y = -pos.y;
	pos.z = dim.z > 1 ? pos.z : 0.0;

	return mul(float4(pos, 1.0), g_worldViewProj);
}
