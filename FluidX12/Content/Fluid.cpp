//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConsts.h"
#include "Fluid.h"
#define _INDEPENDENT_DDS_LOADER_
#include "Advanced/XUSGDDSLoader.h"
#undef _INDEPENDENT_DDS_LOADER_

using namespace std;
using namespace DirectX;
using namespace XUSG;

struct CBSimulation
{
	float TimeStep;
	uint32_t BaseSeed;
};

struct CBPerFrame
{
	XMFLOAT4 EyePos;
	XMFLOAT3X4 LightMapWorld;
	//XMFLOAT4X4 ShadowViewProj;
	XMFLOAT4 LightPos;
	XMFLOAT4 LightColor;
	XMFLOAT4 Ambient;
};

struct CBPerObject
{
	XMFLOAT4X4 WorldViewProjI;
	XMFLOAT4X4 WorldViewProj;
	XMFLOAT3X4 WorldI;
	XMFLOAT3X4 World;
	XMFLOAT3X4 LocalToLight;
};

#ifdef _CPU_CUBE_FACE_CULL_
static_assert(_CPU_CUBE_FACE_CULL_ == 0 || _CPU_CUBE_FACE_CULL_ == 1 || _CPU_CUBE_FACE_CULL_ == 2, "_CPU_CUBE_FACE_CULL_ can only be 0, 1, or 2");
#endif

#if _CPU_CUBE_FACE_CULL_
static inline bool IsCubeFaceVisible(uint8_t face, CXMVECTOR localSpaceEyePt)
{
	const auto& viewComp = XMVectorGetByIndex(localSpaceEyePt, face >> 1);

	return (face & 0x1) ? viewComp > -1.0f : viewComp < 1.0f;
}
#endif

#if _CPU_CUBE_FACE_CULL_ == 1
static inline uint32_t GenVisibilityMask(CXMMATRIX worldI, const XMFLOAT3& eyePt)
{
	const auto localSpaceEyePt = XMVector3Transform(XMLoadFloat3(&eyePt), worldI);

	auto mask = 0u;
	for (uint8_t i = 0; i < 6; ++i)
	{
		const auto isVisible = IsCubeFaceVisible(i, localSpaceEyePt);
		mask |= (isVisible ? 1 : 0) << i;
	}

	return mask;
}
#elif _CPU_CUBE_FACE_CULL_ == 2
struct CBCubeFaceList
{
	XMUINT4 Faces[5];
};

static inline uint8_t GenVisibleCubeFaceList(CBCubeFaceList& faceList, CXMMATRIX worldI, const XMFLOAT3& eyePt)
{
	const auto localSpaceEyePt = XMVector3Transform(XMLoadFloat3(&eyePt), worldI);

	uint8_t count = 0;
	for (uint8_t i = 0; i < 6; ++i)
	{
		if (IsCubeFaceVisible(i, localSpaceEyePt))
		{
			assert(count < 5);
			faceList.Faces[count++].x = i;
		}
	}

	return count;
}
#endif

static inline XMVECTOR ProjectToViewport(uint32_t i, CXMMATRIX worldViewProj, CXMVECTOR viewport)
{
	static const XMVECTOR v[] =
	{
		XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f),
		XMVectorSet(-1.0f, 1.0f, 1.0f, 1.0f),
		XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f),
		XMVectorSet(-1.0f, -1.0f, 1.0f, 1.0f),

		XMVectorSet(-1.0f, 1.0f, -1.0f, 1.0f),
		XMVectorSet(1.0f, 1.0f, -1.0f, 1.0f),
		XMVectorSet(-1.0f, -1.0f, -1.0f, 1.0f),
		XMVectorSet(1.0f, -1.0f, -1.0f, 1.0f),
	};

	auto p = XMVector3TransformCoord(v[i], worldViewProj);
	p *= XMVectorSet(0.5f, -0.5f, 1.0f, 1.0f);
	p += XMVectorSet(0.5f, 0.5f, 0.0f, 0.0f);

	return p * viewport;
}

static inline float EstimateCubeEdgePixelSize(const XMVECTOR v[8])
{
	static const uint8_t ei[][2] =
	{
		{ 0, 1 },
		{ 3, 2 },

		{ 1, 3 },
		{ 2, 0 },

		{ 4, 5 },
		{ 7, 6 },

		{ 5, 7 },
		{ 6, 4 },

		{ 1, 4 },
		{ 6, 3 },

		{ 5, 0 },
		{ 2, 7 }
	};

	auto s = 0.0f;
	for (uint8_t i = 0; i < 12; ++i)
	{
		const auto e = v[ei[i][1]] - v[ei[i][0]];
		s = (max)(XMVectorGetX(XMVector2Length(e)), s);
	}

	return s;
}

static inline uint8_t EstimateCubeMapLOD(uint32_t& raySampleCount, uint8_t numMips, float cubeMapSize,
	CXMMATRIX worldViewProj, CXMVECTOR viewport, float upscale = 2.0f, float raySampleCountScale = 2.0f)
{
	XMVECTOR v[8];
	for (uint8_t i = 0; i < 8; ++i) v[i] = ProjectToViewport(i, worldViewProj, viewport);

	// Calulate the ideal cube-map resolution
	auto s = EstimateCubeEdgePixelSize(v) / upscale;

	// Get the ideal ray sample amount
	auto raySampleAmt = raySampleCountScale * s / sqrtf(3.0f);

	// Clamp the ideal ray sample amount using the user-specified upper bound of ray sample count
	const auto raySampleCnt = static_cast<uint32_t>(ceilf(raySampleAmt));
	raySampleCount = (min)(raySampleCnt, raySampleCount);

	// Inversely derive the cube-map resolution from the clamped ray sample amount
	raySampleAmt = (min)(raySampleAmt, static_cast<float>(raySampleCount));
	s = raySampleAmt / raySampleCountScale * sqrtf(3.0f);

	// Use the more detailed integer level for conservation
	//const auto level = static_cast<uint8_t>(floorf((max)(log2f(cubeMapSize / s), 0.0f)));
	const auto level = static_cast<uint8_t>((max)(log2f(cubeMapSize / s), 0.0f));

	return min<uint8_t>(level, numMips - 1);
}

Fluid::Fluid(const Device::sptr& device) :
	m_device(device), 
	m_lightPt(75.0f, 75.0f, -75.0f),
	m_lightColor(1.0f, 0.7f, 0.3f, XM_PI),
	m_cubeFaceCount(6),
	m_cubeMapLOD(0),
	m_ambient(1.0f, 1.0f, 1.0f, XM_PI * 0.1f),
	m_maxRaySamples(256),
	m_maxLightSamples(64),
	m_frameParity(0),
	m_coeffSH(nullptr),
	m_timeInterval(0.0f)
{
	m_shaderPool = ShaderPool::MakeUnique();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeUnique(device.get());
	m_computePipelineCache = Compute::PipelineCache::MakeUnique(device.get());
	m_pipelineLayoutCache = PipelineLayoutCache::MakeUnique(device.get());

	XMStoreFloat3x4(&m_volumeWorld, XMMatrixScaling(10.0f, 10.0f, 10.0f));
	m_lightMapWorld = m_volumeWorld;
}

Fluid::~Fluid()
{
}

bool Fluid::Init(CommandList* pCommandList, uint32_t width, uint32_t height,
	const DescriptorTableCache::sptr& descriptorTableCache, vector<Resource::uptr>& uploaders,
	Format rtFormat, Format dsFormat, const XMUINT3& gridSize)
{
	m_viewport = XMUINT2(width, height);
	m_descriptorTableCache = descriptorTableCache;
	m_gridSize = gridSize;
	assert(m_gridSize.x == m_gridSize.y);

	// Create resources
	for (uint8_t i = 0; i < 2; ++i)
	{
		m_velocities[i] = Texture3D::MakeUnique();
		N_RETURN(m_velocities[i]->Create(m_device.get(), gridSize.x, gridSize.y, gridSize.z, Format::R16G16B16A16_FLOAT,
			i ? ResourceFlag::ALLOW_UNORDERED_ACCESS : (ResourceFlag::ALLOW_UNORDERED_ACCESS |
				ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS), 1, MemoryFlag::NONE,
				(L"Velocity" + to_wstring(i)).c_str()), false);

		m_colors[i] = Texture3D::MakeUnique();
		N_RETURN(m_colors[i]->Create(m_device.get(), gridSize.x, gridSize.y, gridSize.z, Format::R16G16B16A16_FLOAT,
			ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE,
			(L"Color" + to_wstring(i)).c_str()), false);
	}

	m_incompress = Texture3D::MakeUnique();
	N_RETURN(m_incompress->Create(m_device.get(), gridSize.x, gridSize.y, gridSize.z, Format::R32_FLOAT,
		ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE,
		L"Incompressibility"), false);

	m_lightMapSize = gridSize;
	m_lightMap = Texture3D::MakeUnique();
	N_RETURN(m_lightMap->Create(m_device.get(), m_lightMapSize.x, m_lightMapSize.y, m_lightMapSize.z,
		Format::R11G11B10_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS | ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS,
		1, MemoryFlag::NONE, L"LightMap"), false);

	const uint8_t numMips = 5;
	m_cubeMap = Texture2D::MakeUnique();
	N_RETURN(m_cubeMap->Create(m_device.get(), gridSize.x, gridSize.y, Format::R8G8B8A8_UNORM, 6,
		ResourceFlag::ALLOW_UNORDERED_ACCESS, numMips, 1, true, MemoryFlag::NONE, L"CubeMap"), false);

	//m_cubeDepth = Texture2D::MakeUnique();
	//N_RETURN(m_cubeDepth->Create(m_device.get(), gridSize.x, gridSize.y,  Format::R32_FLOAT, 6,
		//ResourceFlag::ALLOW_UNORDERED_ACCESS, numMips, 1, MemoryFlag::NONE, true, L"CubeDepth"), false);

	// Create constant buffers
	m_cbSimulation = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbSimulation->Create(m_device.get(), sizeof(CBSimulation[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"Fluid.CBSimulation"), false);

	if (m_gridSize.z > 1)
	{
		m_cbPerFrame = ConstantBuffer::MakeUnique();
		N_RETURN(m_cbPerFrame->Create(m_device.get(), sizeof(CBPerFrame[FrameCount]), FrameCount,
			nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"Fluid.CBPerFrame"), false);

		m_cbPerObject = ConstantBuffer::MakeUnique();
		N_RETURN(m_cbPerObject->Create(m_device.get(), sizeof(CBPerObject[FrameCount]), FrameCount,
			nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"Fluid.CBPerObject"), false);
	}

#if _CPU_CUBE_FACE_CULL_ == 2
	m_cbCubeFaceList = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbCubeFaceList->Create(m_device.get(), sizeof(CBCubeFaceList[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBCubeFaceList"), false);
#endif

	ResourceBarrier barrier;
	const auto numBarriers = m_incompress->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	pCommandList->Barrier(numBarriers, &barrier);

	// Create pipelines
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(rtFormat, dsFormat), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void Fluid::SetMaxSamples(uint32_t maxRaySamples, uint32_t maxLightSamples)
{
	m_maxRaySamples = maxRaySamples;
	m_maxLightSamples = maxLightSamples;
}

void Fluid::SetSH(const StructuredBuffer::sptr& coeffSH)
{
	m_coeffSH = coeffSH;
}

void Fluid::UpdateFrame(float timeStep, uint8_t frameIndex,
	const XMFLOAT4X4& view, const XMFLOAT4X4& proj, const XMFLOAT3& eyePt)
{
	// Per-frame for simulation
	{
		const auto pCbData = reinterpret_cast<CBSimulation*>(m_cbSimulation->Map(frameIndex));
		pCbData->TimeStep = timeStep;
		pCbData->BaseSeed = rand();
	}

	// Per-object for simulation
	const auto world = XMLoadFloat3x4(&m_volumeWorld);

	// For Visualization
	if (m_gridSize.z > 1)
	{
		const auto viewProj = XMLoadFloat4x4(&view) * XMLoadFloat4x4(&proj);

		// Per-frame
		{
			const auto pCbData = reinterpret_cast<CBPerFrame*>(m_cbPerFrame->Map(frameIndex));
			pCbData->EyePos = XMFLOAT4(eyePt.x, eyePt.y, eyePt.z, 1.0f);
			pCbData->LightMapWorld = m_lightMapWorld;
			//pCbData->ShadowViewProj = shadowVP;
			pCbData->LightPos = XMFLOAT4(m_lightPt.x, m_lightPt.y, m_lightPt.z, 1.0f);
			pCbData->LightColor = m_lightColor;
			pCbData->Ambient = m_ambient;
		}

		// Per-object
		{
			const auto lightWorld = XMLoadFloat3x4(&m_lightMapWorld);
			const auto lightWorldI = XMMatrixInverse(nullptr, lightWorld);

			const auto world = XMLoadFloat3x4(&m_volumeWorld);
			const auto worldI = XMMatrixInverse(nullptr, world);
			const auto worldViewProj = world * viewProj;

			const auto pCbData = reinterpret_cast<CBPerObject*>(m_cbPerObject->Map(frameIndex));
			XMStoreFloat4x4(&pCbData->WorldViewProj, XMMatrixTranspose(worldViewProj));
			XMStoreFloat4x4(&pCbData->WorldViewProjI, XMMatrixTranspose(XMMatrixInverse(nullptr, worldViewProj)));
			XMStoreFloat3x4(&pCbData->WorldI, worldI);
			XMStoreFloat3x4(&pCbData->World, world);
			XMStoreFloat3x4(&pCbData->LocalToLight, world * lightWorldI);

			{
				m_raySampleCount = m_maxRaySamples;
				const auto numMips = m_cubeMap->GetNumMips();
				const auto cubeMapSize = static_cast<float>(m_cubeMap->GetWidth());
				const auto witdh = static_cast<float>(m_viewport.x);
				const auto height = static_cast<float>(m_viewport.y);
				const auto viewport = XMVectorSet(witdh, height, 1.0f, 1.0f);
				m_cubeMapLOD = EstimateCubeMapLOD(m_raySampleCount, numMips, cubeMapSize, worldViewProj, viewport);

#if _CPU_CUBE_FACE_CULL_ == 1
				m_visibilityMask = GenVisibilityMask(worldI, eyePt);
#elif _CPU_CUBE_FACE_CULL_ == 2
				{
					const auto pCbData = reinterpret_cast<CBCubeFaceList*>(m_cbCubeFaceList->Map(frameIndex));
					m_cubeFaceCount = GenVisibleCubeFaceList(*pCbData, worldI, eyePt);
				}
#endif
			}
		}
	}

	m_timeStep = timeStep;
	if (timeStep > 0.0) m_frameParity = !m_frameParity;
}

void Fluid::Simulate(CommandList* pCommandList, uint8_t frameIndex)
{
	ResourceBarrier barriers[3];

	auto timeStep = m_gridSize.z > 1 ? 1.0f / 60.0f : 1.0f / 800.0f;
	m_timeInterval = m_timeInterval > timeStep ? 0.0f : m_timeInterval;
	m_timeInterval += m_timeStep;
	timeStep = m_timeInterval < timeStep ? 0.0f : timeStep;

	// Advection
	{
		// Set barriers (promotions)
		m_velocities[0]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
		auto numBarriers = m_velocities[1]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
		numBarriers = m_colors[m_frameParity]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
		pCommandList->Barrier(numBarriers, barriers);

		// Set pipeline state
		pCommandList->SetComputePipelineLayout(m_pipelineLayouts[ADVECT]);
		pCommandList->SetPipelineState(m_pipelines[ADVECT]);

		// Set descriptor tables
		pCommandList->SetComputeRootConstantBufferView(0, m_cbSimulation.get(), m_cbSimulation->GetCBVOffset(frameIndex));
		pCommandList->SetComputeDescriptorTable(1, m_srvUavTables[SRV_UAV_TABLE_VECOLITY]);
		pCommandList->SetComputeDescriptorTable(2, m_samplerTables[SAMPLER_TABLE_MIRROR]);
		pCommandList->SetComputeDescriptorTable(3, m_srvUavTables[SRV_UAV_TABLE_COLOR + m_frameParity]);

		pCommandList->Dispatch(DIV_UP(m_gridSize.x, 8), DIV_UP(m_gridSize.y, 8), m_gridSize.z);
	}

	// Projection
	{
		// Set barriers
		auto numBarriers = m_velocities[0]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
		numBarriers = m_velocities[1]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
		numBarriers = m_colors[m_frameParity]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE |
			ResourceState::PIXEL_SHADER_RESOURCE, numBarriers);
		pCommandList->Barrier(numBarriers, barriers);

		// Set pipeline state
		pCommandList->SetComputePipelineLayout(m_pipelineLayouts[PROJECT]);
		pCommandList->SetPipelineState(m_pipelines[PROJECT]);

		// Set descriptor tables
		pCommandList->SetComputeRootConstantBufferView(0, m_cbSimulation.get(), m_cbSimulation->GetCBVOffset(frameIndex));
		pCommandList->SetComputeDescriptorTable(1, m_srvUavTables[SRV_UAV_TABLE_VECOLITY1]);
		
		XMUINT3 numGroups;
		if (m_gridSize.z > 1) // optimized for 3D
		{
			numGroups.x = DIV_UP(m_gridSize.x, 4);
			numGroups.y = DIV_UP(m_gridSize.y, 4);
			numGroups.z = DIV_UP(m_gridSize.z, 4);
		}
		else
		{
			numGroups.x = DIV_UP(m_gridSize.x, 8);
			numGroups.y = DIV_UP(m_gridSize.y, 8);
			numGroups.z = m_gridSize.z;
		}

		pCommandList->Dispatch(numGroups.x, numGroups.y, numGroups.z);
	}
}

void Fluid::Render(CommandList* pCommandList, uint8_t frameIndex, uint8_t flags)
{
	const bool separateLightPass = flags & SEPARATE_LIGHT_PASS;
	const bool cubemapRayMarch = flags & RAY_MARCH_CUBEMAP;

	if (m_gridSize.z > 1)
	{
		if (cubemapRayMarch)
		{
			if (separateLightPass)
			{
				rayMarchL(pCommandList, frameIndex);
				rayMarchV(pCommandList, frameIndex);
			}
			else
			{
				rayMarch(pCommandList, frameIndex);
			}
			renderCube(pCommandList, frameIndex);
		}
		else
		{
			if (separateLightPass)
			{
				rayMarchL(pCommandList, frameIndex);
				rayCastVDirect(pCommandList, frameIndex);
			}
			else
			{
				rayCastDirect(pCommandList, frameIndex);
			}
		}
	}
	else visualizeColor(pCommandList);
}

bool Fluid::createPipelineLayouts()
{
	// Advection
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(1, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetRange(2, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout->SetRange(3, DescriptorType::SRV, 1, 1);
		pipelineLayout->SetRange(3, DescriptorType::UAV, 1, 1, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[ADVECT], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"AdvectionLayout"), false);
	}

	// Projection
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(1, DescriptorType::UAV, 2, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[PROJECT], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"ProjectionLayout"), false);
	}

	if (m_gridSize.z > 1)
	{
		// Ray marching
		{
			const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
			pipelineLayout->SetRange(0, DescriptorType::CBV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
			pipelineLayout->SetRange(1, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
			pipelineLayout->SetRange(2, DescriptorType::SRV, 1, 0);
			pipelineLayout->SetRange(3, DescriptorType::SAMPLER, 1, 0);
			pipelineLayout->SetConstants(4, 3, 2);
			pipelineLayout->SetRootSRV(5, 1);
#if _CPU_CUBE_FACE_CULL_ == 1
			pipelineLayout->SetConstants(6, 1, 3);
#elif _CPU_CUBE_FACE_CULL_ == 2
			pipelineLayout->SetRootCBV(6, 3);
#endif
			X_RETURN(m_pipelineLayouts[RAY_MARCH], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
				PipelineLayoutFlag::NONE, L"RayMarchingLayout"), false);
		}

		// Light space ray marching
		{
			const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
			pipelineLayout->SetRange(0, DescriptorType::CBV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
			pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
			pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
			pipelineLayout->SetRange(3, DescriptorType::SAMPLER, 1, 0);
			pipelineLayout->SetConstants(4, 2, 2);
			pipelineLayout->SetRootSRV(5, 1);
			X_RETURN(m_pipelineLayouts[RAY_MARCH_L], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
				PipelineLayoutFlag::NONE, L"LightSpaceRayMarchingLayout"), false);
		}

		// View space ray marching
		{
			const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
			pipelineLayout->SetRange(0, DescriptorType::CBV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
			pipelineLayout->SetRange(1, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
			pipelineLayout->SetRange(2, DescriptorType::SRV, 2, 0);
			pipelineLayout->SetRange(3, DescriptorType::SAMPLER, 1, 0);
			pipelineLayout->SetConstants(4, 1, 2);
#if _CPU_CUBE_FACE_CULL_ == 1
			pipelineLayout->SetConstants(5, 1, 3);
#elif _CPU_CUBE_FACE_CULL_ == 2
			pipelineLayout->SetRootCBV(5, 3);
#endif
			X_RETURN(m_pipelineLayouts[RAY_MARCH_V], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
				PipelineLayoutFlag::NONE, L"ViewSpaceRayMarchingLayout"), false);
		}

		// Cube rendering
		{
			const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
			pipelineLayout->SetRange(0, DescriptorType::CBV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
			pipelineLayout->SetRange(1, DescriptorType::SRV, 3, 0);
			pipelineLayout->SetRange(2, DescriptorType::SAMPLER, 1, 0);
			pipelineLayout->SetShaderStage(1, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(2, Shader::Stage::PS);
			X_RETURN(m_pipelineLayouts[RENDER_CUBE], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
				PipelineLayoutFlag::NONE, L"CubeLayout"), false);
		}

		// Direct ray casting
		{
			const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
			pipelineLayout->SetRange(0, DescriptorType::CBV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
			pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
			pipelineLayout->SetRange(2, DescriptorType::SAMPLER, 1, 0);
			pipelineLayout->SetConstants(3, 3, 2, 0, Shader::Stage::PS);
			pipelineLayout->SetRootSRV(4, 1, 0, DescriptorFlag::NONE, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(0, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(1, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(2, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(3, Shader::Stage::PS);
			X_RETURN(m_pipelineLayouts[DIRECT_RAY_CAST], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
				PipelineLayoutFlag::NONE, L"DirectRayCastingLayout"), false);
		}

		// Direct view-space ray marching
		{
			const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
			pipelineLayout->SetRange(0, DescriptorType::CBV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
			pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0);
			pipelineLayout->SetRange(2, DescriptorType::SAMPLER, 1, 0);
			pipelineLayout->SetConstants(3, 1, 2, 0, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(0, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(1, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(2, Shader::Stage::PS);
			pipelineLayout->SetShaderStage(3, Shader::Stage::PS);
			X_RETURN(m_pipelineLayouts[DIRECT_RAY_CAST_V], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
				PipelineLayoutFlag::NONE, L"ViewSpaceDirectRayCastingLayout"), false);
		}
	}
	else
	{
		// Visualization
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRange(0, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(1, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout->SetShaderStage(0, Shader::PS);
		pipelineLayout->SetShaderStage(1, Shader::PS);
		X_RETURN(m_pipelineLayouts[VISUALIZE], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"DirectRayCastingLayout"), false);
	}

	return true;
}

bool Fluid::createPipelines(Format rtFormat, Format dsFormat)
{
	auto vsIndex = 0u;
	auto hsIndex = 0u;
	auto dsIndex = 0u;
	auto psIndex = 0u;
	auto csIndex = 0u;

	// Advection
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSAdvect.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[ADVECT]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[ADVECT], state->GetPipeline(m_computePipelineCache.get(), L"Advection"), false);
	}

	// Projection
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, m_gridSize.z > 1 ?
			L"CSProject3D.cso" : L"CSProject2D.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PROJECT]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[PROJECT], state->GetPipeline(m_computePipelineCache.get(), L"Projection"), false);
	}

	// Visualization
	if (m_gridSize.z > 1)
	{
		// Ray marching
		{
			N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSRayMarch.cso"), false);

			const auto state = Compute::State::MakeUnique();
			state->SetPipelineLayout(m_pipelineLayouts[RAY_MARCH]);
			state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
			X_RETURN(m_pipelines[RAY_MARCH], state->GetPipeline(m_computePipelineCache.get(), L"RayMarching"), false);
		}

		// Light space ray marching
		{
			N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSRayMarchL.cso"), false);

			const auto state = Compute::State::MakeUnique();
			state->SetPipelineLayout(m_pipelineLayouts[RAY_MARCH_L]);
			state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
			X_RETURN(m_pipelines[RAY_MARCH_L], state->GetPipeline(m_computePipelineCache.get(), L"LightSpaceRayMarching"), false);
		}

		// View space ray marching
		{
			N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSRayMarchV.cso"), false);

			const auto state = Compute::State::MakeUnique();
			state->SetPipelineLayout(m_pipelineLayouts[RAY_MARCH_V]);
			state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
			X_RETURN(m_pipelines[RAY_MARCH_V], state->GetPipeline(m_computePipelineCache.get(), L"ViewSpaceRayMarching"), false);
		}

		// Cube rendering
		{
			N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSCube.cso"), false);
			N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSCube.cso"), false);

			const auto state = Graphics::State::MakeUnique();
			state->SetPipelineLayout(m_pipelineLayouts[RENDER_CUBE]);
			state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
			state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex++));
			state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
			state->RSSetState(Graphics::CULL_FRONT, m_graphicsPipelineCache.get()); // Front-face culling for interior surfaces
			state->DSSetState(Graphics::DEPTH_STENCIL_NONE, m_graphicsPipelineCache.get());
			state->OMSetBlendState(Graphics::PREMULTIPLITED, m_graphicsPipelineCache.get());
			state->OMSetRTVFormats(&rtFormat, 1);
			X_RETURN(m_pipelines[RENDER_CUBE], state->GetPipeline(m_graphicsPipelineCache.get(), L"RayCasting"), false);
		}

		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSScreenQuad.cso"), false);

		// Direct ray casting
		{
			N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSRayCast.cso"), false);

			const auto state = Graphics::State::MakeUnique();
			state->SetPipelineLayout(m_pipelineLayouts[DIRECT_RAY_CAST]);
			state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex));
			state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex++));
			state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
			state->DSSetState(Graphics::DEPTH_STENCIL_NONE, m_graphicsPipelineCache.get());
			state->OMSetBlendState(Graphics::PREMULTIPLITED, m_graphicsPipelineCache.get());
			state->OMSetRTVFormats(&rtFormat, 1);
			X_RETURN(m_pipelines[DIRECT_RAY_CAST], state->GetPipeline(m_graphicsPipelineCache.get(), L"DirectRayCasting"), false);
		}

		// View space direct Ray casting 
		{
			N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSRayCastV.cso"), false);

			const auto state = Graphics::State::MakeUnique();
			state->SetPipelineLayout(m_pipelineLayouts[DIRECT_RAY_CAST_V]);
			state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
			state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex++));
			state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
			state->DSSetState(Graphics::DEPTH_STENCIL_NONE, m_graphicsPipelineCache.get());
			state->OMSetBlendState(Graphics::PREMULTIPLITED, m_graphicsPipelineCache.get());
			state->OMSetRTVFormats(&rtFormat, 1);
			X_RETURN(m_pipelines[DIRECT_RAY_CAST_V], state->GetPipeline(m_graphicsPipelineCache.get(), L"ViewSpaceDirectRayCasting"), false);
		}
	}
	else
	{
		// Visualization
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSScreenQuad.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSVisualizeColor.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[VISUALIZE]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex++));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
		state->OMSetBlendState(Graphics::PREMULTIPLITED, m_graphicsPipelineCache.get());
		state->DSSetState(Graphics::DEPTH_STENCIL_NONE, m_graphicsPipelineCache.get());
		state->OMSetRTVFormats(&rtFormat, 1);
		X_RETURN(m_pipelines[VISUALIZE], state->GetPipeline(m_graphicsPipelineCache.get(), L"Visualization"), false);
	}

	return true;
}

bool Fluid::createDescriptorTables()
{
	// Create CBV tables
	if (m_gridSize.z > 1)
	{
		for (uint8_t i = 0; i < FrameCount; ++i)
		{
			const auto descriptorTable = Util::DescriptorTable::MakeUnique();
			const Descriptor descriptors[] =
			{
				m_cbPerObject->GetCBV(i),
				m_cbPerFrame->GetCBV(i)
			};
			descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
			X_RETURN(m_cbvTables[i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
		}
	}

	// Create SRV and UAV tables
	for (uint8_t i = 0; i < 2; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_velocities[i]->GetSRV(),
			m_velocities[!i]->GetUAV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvUavTables[SRV_UAV_TABLE_VECOLITY + i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	{
		// Create incompressibility UAV
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_incompress->GetUAV());
		X_RETURN(m_srvUavTables[UAV_TABLE_INCOMPRESS], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	for (uint8_t i = 0; i < 2; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_colors[!i]->GetSRV(),
			m_colors[i]->GetUAV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvUavTables[SRV_UAV_TABLE_COLOR + i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	for (uint8_t i = 0; i < 2; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_colors[(i + 1) % 2]->GetSRV(),
			m_lightMap->GetSRV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvUavTables[SRV_TABLE_RAY_MARCH + i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	// Create UAV and SRV table
	const uint8_t numMips = m_cubeMap->GetNumMips();
	m_uavMipTables.resize(numMips);
	for (uint8_t i = 0; i < numMips; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_cubeMap->GetUAV(i),
			//m_cubeDepth->GetUAV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_uavMipTables[i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	// Create SRV tables
	m_srvMipTables.resize(numMips);
	for (uint8_t i = 0; i < numMips; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_cubeMap->GetSRV(i),
			//m_cubeDepth->GetSRV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvMipTables[i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0,1, &m_lightMap->GetUAV());
		X_RETURN(m_srvUavTables[UAV_TABLE_LIGHT_MAP], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}
	
	// Create the samplers
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const auto samplerLinearMirror = SamplerPreset::LINEAR_MIRROR;
		descriptorTable->SetSamplers(0, 1, &samplerLinearMirror, m_descriptorTableCache.get());
		X_RETURN(m_samplerTables[SAMPLER_TABLE_MIRROR], descriptorTable->GetSamplerTable(m_descriptorTableCache.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const auto samplerLinearClamp = SamplerPreset::LINEAR_CLAMP;
		descriptorTable->SetSamplers(0, 1, &samplerLinearClamp, m_descriptorTableCache.get());
		X_RETURN(m_samplerTables[SAMPLER_TABLE_CLAMP], descriptorTable->GetSamplerTable(m_descriptorTableCache.get()), false);
	}

	return true;
}

void Fluid::visualizeColor(const CommandList* pCommandList)
{
	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[VISUALIZE]);
	pCommandList->SetPipelineState(m_pipelines[VISUALIZE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);

	// Set descriptor tables
	pCommandList->SetGraphicsDescriptorTable(0, m_srvUavTables[SRV_UAV_TABLE_COLOR + !m_frameParity]);
	pCommandList->SetGraphicsDescriptorTable(1, m_samplerTables[SAMPLER_TABLE_CLAMP]);

	pCommandList->Draw(3, 1, 0, 0);
}

void Fluid::rayMarch(CommandList* pCommandList, uint8_t frameIndex)
{
	// Set barriers
	ResourceBarrier barriers[6];
	auto numBarriers = 0u;
	for (uint8_t i = 0; i < 6; ++i)
		numBarriers = m_cubeMap->SetBarrier(barriers, m_cubeMapLOD, ResourceState::UNORDERED_ACCESS, numBarriers, i);
	pCommandList->Barrier(numBarriers, barriers);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[RAY_MARCH]);
	pCommandList->SetPipelineState(m_pipelines[RAY_MARCH]);

	// Set descriptor tables
	pCommandList->SetComputeDescriptorTable(0, m_cbvTables[frameIndex]);
	pCommandList->SetComputeDescriptorTable(1, m_uavMipTables[m_cubeMapLOD]);
	pCommandList->SetComputeDescriptorTable(2, m_srvUavTables[SRV_TABLE_RAY_MARCH + !m_frameParity]);
	pCommandList->SetComputeDescriptorTable(3, m_samplerTables[SAMPLER_TABLE_CLAMP]);
	pCommandList->SetCompute32BitConstant(4, m_raySampleCount);
	pCommandList->SetCompute32BitConstant(4, m_coeffSH ? 1 : 0, 1);
	pCommandList->SetCompute32BitConstant(4, m_maxLightSamples, 2);
	if (m_coeffSH) pCommandList->SetComputeRootShaderResourceView(5, m_coeffSH.get());
#if _CPU_CUBE_FACE_CULL_ == 1
	pCommandList->SetCompute32BitConstant(6, m_visibilityMask);
#elif _CPU_CUBE_FACE_CULL_ == 2
	pCommandList->SetComputeRootConstantBufferView(6, m_cbCubeFaceList.get(), m_cbCubeFaceList->GetCBVOffset(frameIndex));
#endif

	// Dispatch cube
	const auto gridSize = m_gridSize.x >> m_cubeMapLOD;
	pCommandList->Dispatch(DIV_UP(gridSize, 8), DIV_UP(gridSize, 8), m_cubeFaceCount);
}

void Fluid::rayMarchL(const CommandList* pCommandList, uint8_t frameIndex)
{
	// Set barrier
	ResourceBarrier barrier;
	m_lightMap->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[RAY_MARCH_L]);
	pCommandList->SetPipelineState(m_pipelines[RAY_MARCH_L]);

	// Set descriptor tables
	pCommandList->SetComputeDescriptorTable(0, m_cbvTables[frameIndex]);
	//pCommandList->SetComputeRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetComputeDescriptorTable(1, m_srvUavTables[SRV_TABLE_RAY_MARCH + !m_frameParity]);
	pCommandList->SetComputeDescriptorTable(2, m_srvUavTables[UAV_TABLE_LIGHT_MAP]);
	pCommandList->SetComputeDescriptorTable(3, m_samplerTables[SAMPLER_TABLE_CLAMP]);
	pCommandList->SetCompute32BitConstant(4, m_maxLightSamples);
	pCommandList->SetCompute32BitConstant(4, m_coeffSH ? 1 : 0, 1);
	if (m_coeffSH) pCommandList->SetComputeRootShaderResourceView(5, m_coeffSH.get());

	// Dispatch grid
	pCommandList->Dispatch(DIV_UP(m_lightMapSize.x, 4), DIV_UP(m_lightMapSize.y, 4), DIV_UP(m_lightMapSize.z, 4));
}

void Fluid::rayMarchV(CommandList* pCommandList, uint8_t frameIndex)
{
	// Set barriers
	ResourceBarrier barriers[7];
	auto numBarriers = m_lightMap->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
	for (uint8_t i = 0; i < 6; ++i)
		numBarriers = m_cubeMap->SetBarrier(barriers, m_cubeMapLOD, ResourceState::UNORDERED_ACCESS, numBarriers, i);
	pCommandList->Barrier(numBarriers, barriers);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[RAY_MARCH_V]);
	pCommandList->SetPipelineState(m_pipelines[RAY_MARCH_V]);

	// Set descriptor tables
	pCommandList->SetComputeDescriptorTable(0, m_cbvTables[frameIndex]);
	//pCommandList->SetComputeRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetComputeDescriptorTable(1, m_uavMipTables[m_cubeMapLOD]);
	pCommandList->SetComputeDescriptorTable(2, m_srvUavTables[SRV_TABLE_RAY_MARCH + !m_frameParity]);
	pCommandList->SetComputeDescriptorTable(3, m_samplerTables[SAMPLER_TABLE_CLAMP]);
	pCommandList->SetCompute32BitConstant(4, m_raySampleCount);
#if _CPU_CUBE_FACE_CULL_ == 1
	pCommandList->SetCompute32BitConstant(5, m_visibilityMask);
#elif _CPU_CUBE_FACE_CULL_ == 2
	pCommandList->SetComputeRootConstantBufferView(5, m_cbCubeFaceList.get(), m_cbCubeFaceList->GetCBVOffset(frameIndex));
#endif

	// Dispatch cube
	const auto gridSize = m_gridSize.x >> m_cubeMapLOD;
	pCommandList->Dispatch(DIV_UP(gridSize, 8), DIV_UP(gridSize, 8), m_cubeFaceCount);
}

void Fluid::renderCube(CommandList* pCommandList, uint8_t frameIndex)
{
	// Set barriers
	ResourceBarrier barriers[6];
	auto numBarriers = 0u;
	for (uint8_t i = 0; i < 6; ++i)
		numBarriers = m_cubeMap->SetBarrier(barriers, m_cubeMapLOD, ResourceState::PIXEL_SHADER_RESOURCE, numBarriers, i);
	pCommandList->Barrier(numBarriers, barriers);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[RENDER_CUBE]);
	pCommandList->SetPipelineState(m_pipelines[RENDER_CUBE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLESTRIP);

	// Set descriptor tables
	pCommandList->SetGraphicsDescriptorTable(0, m_cbvTables[frameIndex]);
	//pCommandList->SetGraphicsRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetGraphicsDescriptorTable(1, m_srvMipTables[m_cubeMapLOD]);
	pCommandList->SetGraphicsDescriptorTable(2, m_samplerTables[SAMPLER_TABLE_CLAMP]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLESTRIP);
	pCommandList->Draw(4, 6, 0, 0);
}

void Fluid::rayCastDirect(const CommandList* pCommandList, uint8_t frameIndex)
{
	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[DIRECT_RAY_CAST]);
	pCommandList->SetPipelineState(m_pipelines[DIRECT_RAY_CAST]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);

	// Set descriptor tables
	pCommandList->SetGraphicsDescriptorTable(0, m_cbvTables[frameIndex]);
	//pCommandList->SetGraphicsRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetGraphicsDescriptorTable(1, m_srvUavTables[SRV_TABLE_RAY_MARCH + !m_frameParity]);
	pCommandList->SetGraphicsDescriptorTable(2, m_samplerTables[SAMPLER_TABLE_CLAMP]);
	pCommandList->SetGraphics32BitConstant(3, m_maxRaySamples);
	pCommandList->SetGraphics32BitConstant(3, m_coeffSH ? 1 : 0, 1);
	pCommandList->SetGraphics32BitConstant(3, m_maxLightSamples, 2);
	if (m_coeffSH) pCommandList->SetGraphicsRootShaderResourceView(4, m_coeffSH.get());
	pCommandList->Draw(3, 1, 0, 0);
}

void Fluid::rayCastVDirect(CommandList* pCommandList, uint8_t frameIndex)
{
	// Set barriers
	ResourceBarrier barriers;
	auto numBarriers = m_lightMap->SetBarrier(&barriers, ResourceState::PIXEL_SHADER_RESOURCE);
	pCommandList->Barrier(numBarriers, &barriers);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[DIRECT_RAY_CAST_V]);
	pCommandList->SetPipelineState(m_pipelines[DIRECT_RAY_CAST_V]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);

	// Set descriptor tables
	pCommandList->SetGraphicsDescriptorTable(0, m_cbvTables[frameIndex]);
	//pCommandList->SetGraphicsRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetGraphicsDescriptorTable(1, m_srvUavTables[SRV_TABLE_RAY_MARCH + !m_frameParity]);
	pCommandList->SetGraphicsDescriptorTable(2, m_samplerTables[SAMPLER_TABLE_CLAMP]);
	pCommandList->SetGraphics32BitConstant(3, m_raySampleCount);

	pCommandList->Draw(3, 1, 0, 0);
}
