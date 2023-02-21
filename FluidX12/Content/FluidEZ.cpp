//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConsts.h"
#include "FluidEZ.h"

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
};

struct CBSampleRes
{
	uint32_t NumSamples;
	uint32_t HasLightProbes;
	uint32_t NumLightSamples;
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

FluidEZ::FluidEZ() :
	m_lightPt(75.0f, 75.0f, -75.0f),
	m_lightColor(1.0f, 0.7f, 0.3f, XM_PI * 3.0f),
	m_cubeFaceCount(6),
	m_cubeMapLOD(0),
	m_ambient(1.0f, 1.0f, 1.0f, XM_PI * 1.5f),
	m_maxRaySamples(192),
	m_maxLightSamples(64),
	m_frameParity(0),
	m_coeffSH(nullptr),
	m_timeInterval(0.0f)
{
	m_shaderLib = ShaderLib::MakeUnique();

	XMStoreFloat3x4(&m_volumeWorld, XMMatrixScaling(10.0f, 10.0f, 10.0f));
}

FluidEZ::~FluidEZ()
{
}

bool FluidEZ::Init(XUSG::CommandList* pCommandList, uint32_t width, uint32_t height,
	vector<Resource::uptr>& uploaders, Format rtFormat, Format dsFormat, const XMUINT3& gridSize)
{
	const auto pDevice = pCommandList->GetDevice();

	m_viewport = XMUINT2(width, height);
	m_gridSize = gridSize;
	assert(m_gridSize.x == m_gridSize.y);

	// Create resources
	for (uint8_t i = 0; i < 2; ++i)
	{
		m_velocities[i] = Texture3D::MakeUnique();
		XUSG_N_RETURN(m_velocities[i]->Create(pDevice, gridSize.x, gridSize.y, gridSize.z, Format::R16G16B16A16_FLOAT,
			ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE, (L"VelocityEZ" + to_wstring(i)).c_str()), false);

		m_colors[i] = Texture3D::MakeUnique();
		XUSG_N_RETURN(m_colors[i]->Create(pDevice, gridSize.x, gridSize.y, gridSize.z, Format::R16G16B16A16_FLOAT,
			ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE,
			(L"ColorEZ" + to_wstring(i)).c_str()), false);
	}

	m_incompress = Texture3D::MakeUnique();
	XUSG_N_RETURN(m_incompress->Create(pDevice, gridSize.x, gridSize.y, gridSize.z, Format::R32_FLOAT,
		ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE,
		L"IncompressibilityEZ"), false);

	m_lightMapSize = gridSize;
	m_lightMap = Texture3D::MakeUnique();
	XUSG_N_RETURN(m_lightMap->Create(pDevice, m_lightMapSize.x, m_lightMapSize.y, m_lightMapSize.z,
		Format::R11G11B10_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS | ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS,
		1, MemoryFlag::NONE, L"LightMapEZ"), false);

	const uint8_t numMips = 5;
	m_cubeMap = Texture2D::MakeUnique();
	XUSG_N_RETURN(m_cubeMap->Create(pDevice, gridSize.x, gridSize.y, Format::R8G8B8A8_UNORM, 6,
		ResourceFlag::ALLOW_UNORDERED_ACCESS, numMips, 1, true, MemoryFlag::NONE, L"CubeMapEZ"), false);

	//m_cubeDepth = Texture2D::MakeUnique();
	//XUSG_N_RETURN(m_cubeDepth->Create(pDevice, gridSize.x, gridSize.y,  Format::R32_FLOAT, 6,
	//	ResourceFlag::ALLOW_UNORDERED_ACCESS, numMips, 1, true, MemoryFlag::NONE, L"CubeDepthEZ"), false);

	m_nullBuffer = Buffer::MakeUnique();
	XUSG_N_RETURN(m_nullBuffer->Create(pDevice, 0, ResourceFlag::ALLOW_UNORDERED_ACCESS,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, MemoryFlag::NONE, L"NullBuffer"), false);

	// Create constant buffers
	m_cbSimulation = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbSimulation->Create(pDevice, sizeof(CBSimulation[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"FluidEZ.CBSimulation"), false);

	if (m_gridSize.z > 1)
	{
		m_cbPerFrame = ConstantBuffer::MakeUnique();
		XUSG_N_RETURN(m_cbPerFrame->Create(pDevice, sizeof(CBPerFrame[FrameCount]), FrameCount,
			nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"FluidEZ.CBPerFrame"), false);

		m_cbPerObject = ConstantBuffer::MakeUnique();
		XUSG_N_RETURN(m_cbPerObject->Create(pDevice, sizeof(CBPerObject[FrameCount]), FrameCount,
			nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"FluidEZ.CBPerObject"), false);
	}

	for (uint8_t i = 0; i < NUM_CB_SAMPLE_RES; ++i)
	{
		auto& cbSampleRes = m_cbSampleRes[i];
		cbSampleRes = ConstantBuffer::MakeUnique();
		XUSG_N_RETURN(cbSampleRes->Create(pDevice, sizeof(CBSampleRes[FrameCount]), FrameCount, nullptr,
			MemoryType::UPLOAD, MemoryFlag::NONE, (L"FluidEZ.CBSampleRes" + to_wstring(i)).c_str()), false);
	}

#if _CPU_CUBE_FACE_CULL_ == 1
	m_cbCubeFaceCull = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbCubeFaceCull->Create(pDevice, sizeof(uint32_t[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBCubeFaceVisEZ"), false);
#elif _CPU_CUBE_FACE_CULL_ == 2
	m_cbCubeFaceList = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbCubeFaceList->Create(m_device.get(), sizeof(CBCubeFaceList[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBCubeFaceListEZ"), false);
#endif

	// Create shaders
	return createShaders();
}

void FluidEZ::SetMaxSamples(uint32_t maxRaySamples, uint32_t maxLightSamples)
{
	m_maxRaySamples = maxRaySamples;
	m_maxLightSamples = maxLightSamples;
}

void FluidEZ::SetSH(const StructuredBuffer::sptr& coeffSH)
{
	m_coeffSH = coeffSH;
}

void FluidEZ::UpdateFrame(float timeStep, uint8_t frameIndex,
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
			//pCbData->ShadowViewProj = shadowVP;
			pCbData->LightPos = XMFLOAT4(m_lightPt.x, m_lightPt.y, m_lightPt.z, 1.0f);
			pCbData->LightColor = m_lightColor;
			pCbData->Ambient = m_ambient;
		}

		// Per-object
		{
			const auto world = XMLoadFloat3x4(&m_volumeWorld);
			const auto worldI = XMMatrixInverse(nullptr, world);
			const auto worldViewProj = world * viewProj;

			const auto pCbData = reinterpret_cast<CBPerObject*>(m_cbPerObject->Map(frameIndex));
			XMStoreFloat4x4(&pCbData->WorldViewProj, XMMatrixTranspose(worldViewProj));
			XMStoreFloat4x4(&pCbData->WorldViewProjI, XMMatrixTranspose(XMMatrixInverse(nullptr, worldViewProj)));
			XMStoreFloat3x4(&pCbData->WorldI, worldI);
			XMStoreFloat3x4(&pCbData->World, world);

			{
				m_raySampleCount = m_maxRaySamples;
				const auto numMips = m_cubeMap->GetNumMips();
				const auto cubeMapSize = static_cast<float>(m_cubeMap->GetWidth());
				const auto witdh = static_cast<float>(m_viewport.x);
				const auto height = static_cast<float>(m_viewport.y);
				const auto viewport = XMVectorSet(witdh, height, 1.0f, 1.0f);
				m_cubeMapLOD = EstimateCubeMapLOD(m_raySampleCount, numMips, cubeMapSize, worldViewProj, viewport);

				{
					const auto pCbData = static_cast<CBSampleRes*>(m_cbSampleRes[CB_SAMPLE_RES]->Map(frameIndex));
					pCbData->NumSamples = m_raySampleCount;
					pCbData->HasLightProbes = m_coeffSH ? 1 : 0;
					pCbData->NumLightSamples = m_maxLightSamples;
				}

				{
					const auto pCbData = static_cast<CBSampleRes*>(m_cbSampleRes[CB_SAMPLE_RES_L]->Map(frameIndex));
					pCbData->NumSamples = m_maxLightSamples;
					pCbData->HasLightProbes = m_coeffSH ? 1 : 0;
					pCbData->NumLightSamples = m_maxLightSamples;
				}

#if _CPU_CUBE_FACE_CULL_ == 1
				{
					const auto pCbData = static_cast<uint32_t*>(m_cbCubeFaceCull->Map(frameIndex));
					*pCbData = GenVisibilityMask(worldI, eyePt);
				}
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

void FluidEZ::Simulate(EZ::CommandList* pCommandList, uint8_t frameIndex)
{
	auto timeStep = m_gridSize.z > 1 ? 1.0f / 60.0f : 1.0f / 800.0f;
	m_timeInterval = m_timeInterval > timeStep ? 0.0f : m_timeInterval;
	m_timeInterval += m_timeStep;
	timeStep = m_timeInterval < timeStep ? 0.0f : timeStep;

	// Advection
	{
		// Set pipeline state
		pCommandList->SetComputeShader(m_shaders[CS_ADVECT]);

		// Set UAVs
		const EZ::ResourceView uavs[] =
		{
			EZ::GetUAV(m_velocities[1].get()),
			EZ::GetUAV(m_colors[m_frameParity].get())
		};
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::UAV, 0, static_cast<uint32_t>(size(uavs)), uavs);

		// Set CBV
		const auto cbv = EZ::GetCBV(m_cbSimulation.get(), frameIndex);
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, 0, 1, &cbv);

		// Set SRVs
		const EZ::ResourceView srvs[] =
		{
			EZ::GetSRV(m_velocities[0].get()),
			EZ::GetSRV(m_colors[!m_frameParity].get())
		};
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 0, static_cast<uint32_t>(size(srvs)), srvs);

		// Set sampler
		const auto sampler = SamplerPreset::LINEAR_CLAMP;
		pCommandList->SetSamplerStates(Shader::Stage::CS, 0, 1, &sampler);

		pCommandList->Dispatch(XUSG_DIV_UP(m_gridSize.x, 8), XUSG_DIV_UP(m_gridSize.y, 8), m_gridSize.z);
	}

	// Projection
	{
		// Set pipeline state
		pCommandList->SetComputeShader(m_shaders[m_gridSize.z > 1 ? CS_PROJECT_3D : CS_PROJECT_2D]);

		// Set UAVs
		const EZ::ResourceView uavs[] =
		{
			EZ::GetUAV(m_velocities[0].get()),
			EZ::GetUAV(m_incompress.get())
		};
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::UAV, 0, static_cast<uint32_t>(size(uavs)), uavs);

		// Set CBV
		const auto cbv = EZ::GetCBV(m_cbSimulation.get(), frameIndex);
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, 0, 1, &cbv);

		// Set SRV
		const auto srv = EZ::GetSRV(m_velocities[1].get());
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 0, 1, &srv);

		XMUINT3 numGroups;
		if (m_gridSize.z > 1) // optimized for 3D
		{
			numGroups.x = XUSG_DIV_UP(m_gridSize.x, 4);
			numGroups.y = XUSG_DIV_UP(m_gridSize.y, 4);
			numGroups.z = XUSG_DIV_UP(m_gridSize.z, 4);
		}
		else
		{
			numGroups.x = XUSG_DIV_UP(m_gridSize.x, 8);
			numGroups.y = XUSG_DIV_UP(m_gridSize.y, 8);
			numGroups.z = m_gridSize.z;
		}

		pCommandList->Dispatch(numGroups.x, numGroups.y, numGroups.z);
	}
}

void FluidEZ::Render(EZ::CommandList* pCommandList, uint8_t frameIndex, uint8_t flags)
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

bool FluidEZ::createShaders()
{
	auto vsIndex = 0u;
	auto psIndex = 0u;
	auto csIndex = 0u;

	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSAdvect.cso"), false);
	m_shaders[CS_ADVECT] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);

	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSProject3D.cso"), false);
	m_shaders[CS_PROJECT_3D] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSProject2D.cso"), false);
	m_shaders[CS_PROJECT_2D] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSRayMarch.cso"), false);
	m_shaders[CS_RAY_MARCH] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSRayMarchL.cso"), false);
	m_shaders[CS_RAY_MARCH_L] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSRayMarchV.cso"), false);
	m_shaders[CS_RAY_MARCH_V] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSCube.cso"), false);
	m_shaders[VS_CUBE] = m_shaderLib->GetShader(Shader::Stage::VS, vsIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, psIndex, L"PSCube.cso"), false);
	m_shaders[PS_CUBE] = m_shaderLib->GetShader(Shader::Stage::PS, psIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSScreenQuad.cso"), false);
	m_shaders[VS_SCREEN_QUAD] = m_shaderLib->GetShader(Shader::Stage::VS, vsIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, psIndex, L"PSRayCast.cso"), false);
	m_shaders[PS_RAY_CAST] = m_shaderLib->GetShader(Shader::Stage::PS, psIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, psIndex, L"PSRayCastV.cso"), false);
	m_shaders[PS_RAY_CAST_V] = m_shaderLib->GetShader(Shader::Stage::PS, psIndex++);
	
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, psIndex, L"PSVisualizeColor.cso"), false);
	m_shaders[PS_VISUALIZE_COLOR] = m_shaderLib->GetShader(Shader::Stage::PS, psIndex++);

	return true;
}

void FluidEZ::visualizeColor(EZ::CommandList* pCommandList)
{
	// Set pipeline state
	pCommandList->SetGraphicsShader(Shader::Stage::VS, m_shaders[VS_SCREEN_QUAD]);
	pCommandList->SetGraphicsShader(Shader::Stage::PS, m_shaders[PS_VISUALIZE_COLOR]);
	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
	pCommandList->OMSetBlendState(Graphics::PREMULTIPLITED);
	pCommandList->DSSetState(Graphics::DEPTH_STENCIL_NONE);

	// Set SRV
	const auto srv = EZ::GetSRV(m_colors[m_frameParity].get());
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::SRV, 0, 1, &srv);

	// Set sampler
	const auto sampler = SamplerPreset::LINEAR_CLAMP;
	pCommandList->SetSamplerStates(Shader::Stage::PS, 0, 1, &sampler);

	pCommandList->Draw(3, 1, 0, 0);
}

void FluidEZ::rayMarch(EZ::CommandList* pCommandList, uint8_t frameIndex)
{
	// Set pipeline state
	pCommandList->SetComputeShader(m_shaders[CS_RAY_MARCH]);

	// Set UAV
	const auto uav = EZ::GetUAV(m_cubeMap.get(), m_cubeMapLOD);
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::UAV, 0, 1, &uav);

	// Set CBVs
	const EZ::ResourceView cbvs[] =
	{
		EZ::GetCBV(m_cbPerObject.get(), frameIndex),
		EZ::GetCBV(m_cbPerFrame.get(), frameIndex),
		EZ::GetCBV(m_cbSampleRes[CB_SAMPLE_RES].get(), frameIndex)
	};
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, 0, static_cast<uint32_t>(size(cbvs)), cbvs);

#if _CPU_CUBE_FACE_CULL_ == 1
	const auto cbv = EZ::GetCBV(m_cbCubeFaceCull.get(), frameIndex);
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, static_cast<uint32_t>(size(cbvs)), 1, &cbv);
#elif _CPU_CUBE_FACE_CULL_ == 2
	const auto cbv = EZ::GetCBV(m_cbCubeFaceList.get(), frameIndex);
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, static_cast<uint32_t>(size(cbvs)), 1, &cbv);
#endif

	// Set SRVs
	{
		const auto srv = EZ::GetSRV(m_colors[m_frameParity].get());
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 0, 1, &srv);
	}

	if (m_coeffSH)
	{
		const auto srv = EZ::GetSRV(m_coeffSH.get());
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 1, 1, &srv);
	}

	// Set sampler
	const auto sampler = SamplerPreset::LINEAR_CLAMP;
	pCommandList->SetSamplerStates(Shader::Stage::CS, 0, 1, &sampler);

	// Dispatch cube
	const auto gridSize = m_gridSize.x >> m_cubeMapLOD;
	pCommandList->Dispatch(XUSG_DIV_UP(gridSize, 8), XUSG_DIV_UP(gridSize, 8), m_cubeFaceCount);
}

void FluidEZ::rayMarchL(EZ::CommandList* pCommandList, uint8_t frameIndex)
{
	// Set pipeline state
	pCommandList->SetComputeShader(m_shaders[CS_RAY_MARCH_L]);

	// Set UAV
	const auto uav = EZ::GetUAV(m_lightMap.get());
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::UAV, 0, 1, &uav);

	// Set CBVs
	const EZ::ResourceView cbvs[] =
	{
		EZ::GetCBV(m_cbPerObject.get(), frameIndex),
		EZ::GetCBV(m_cbPerFrame.get(), frameIndex),
		EZ::GetCBV(m_cbSampleRes[CB_SAMPLE_RES_L].get(), frameIndex)
	};
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, 0, static_cast<uint32_t>(size(cbvs)), cbvs);

	// Set SRVs
	{
		const EZ::ResourceView srvs[] =
		{
			EZ::GetSRV(m_colors[m_frameParity].get()),
			EZ::GetSRV(m_nullBuffer.get()) // Workaround for Tier 2 GPUs
		};
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 0, static_cast<uint32_t>(size(srvs)), srvs);
	}

	if (m_coeffSH)
	{
		const auto srv = EZ::GetSRV(m_coeffSH.get());
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 1, 1, &srv);
	}

	// Set sampler
	const auto sampler = SamplerPreset::LINEAR_CLAMP;
	pCommandList->SetSamplerStates(Shader::Stage::CS, 0, 1, &sampler);

	// Dispatch grid
	pCommandList->Dispatch(XUSG_DIV_UP(m_lightMapSize.x, 4), XUSG_DIV_UP(m_lightMapSize.y, 4), XUSG_DIV_UP(m_lightMapSize.z, 4));
}

void FluidEZ::rayMarchV(EZ::CommandList* pCommandList, uint8_t frameIndex)
{
	// Set pipeline state
	pCommandList->SetComputeShader(m_shaders[CS_RAY_MARCH_V]);

	// Set UAV
	const auto uav = EZ::GetUAV(m_cubeMap.get(), m_cubeMapLOD);
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::UAV, 0, 1, &uav);

	// Set CBVs
	const EZ::ResourceView cbvs[] =
	{
		EZ::GetCBV(m_cbPerObject.get(), frameIndex),
		EZ::GetCBV(m_cbPerFrame.get(), frameIndex),
		EZ::GetCBV(m_cbSampleRes[CB_SAMPLE_RES].get(), frameIndex)
	};
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, 0, static_cast<uint32_t>(size(cbvs)), cbvs);

#if _CPU_CUBE_FACE_CULL_ == 1
	const auto cbv = EZ::GetCBV(m_cbCubeFaceCull.get(), frameIndex);
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, static_cast<uint32_t>(size(cbvs)), 1, &cbv);
#elif _CPU_CUBE_FACE_CULL_ == 2
	const auto cbv = EZ::GetCBV(m_cbCubeFaceList.get(), frameIndex);
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, static_cast<uint32_t>(size(cbvs)), 1, &cbv);
#endif

	// Set SRVs
	const EZ::ResourceView srvs[] =
	{
		EZ::GetSRV(m_colors[m_frameParity].get()),
		EZ::GetSRV(m_lightMap.get())
	};
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 0, static_cast<uint32_t>(size(srvs)), srvs);

	// Set sampler
	const auto sampler = SamplerPreset::LINEAR_CLAMP;
	pCommandList->SetSamplerStates(Shader::Stage::CS, 0, 1, &sampler);

	// Dispatch cube
	const auto gridSize = m_gridSize.x >> m_cubeMapLOD;
	pCommandList->Dispatch(XUSG_DIV_UP(gridSize, 8), XUSG_DIV_UP(gridSize, 8), m_cubeFaceCount);
}

void FluidEZ::renderCube(EZ::CommandList* pCommandList, uint8_t frameIndex)
{
	// Set pipeline state
	pCommandList->SetGraphicsShader(Shader::Stage::VS, m_shaders[VS_CUBE]);
	pCommandList->SetGraphicsShader(Shader::Stage::PS, m_shaders[PS_CUBE]);
	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLESTRIP);
	pCommandList->RSSetState(Graphics::CULL_FRONT);
	pCommandList->DSSetState(Graphics::DEPTH_STENCIL_NONE);
	pCommandList->OMSetBlendState(Graphics::PREMULTIPLITED);

	// Set CBVs
	const EZ::ResourceView cbvs[] =
	{
		EZ::GetCBV(m_cbPerObject.get(), frameIndex),
		EZ::GetCBV(m_cbPerFrame.get(), frameIndex),
		EZ::GetCBV(m_cbSampleRes[CB_SAMPLE_RES].get(), frameIndex)
	};
	pCommandList->SetResources(Shader::Stage::VS, DescriptorType::CBV, 0, 1, cbvs);
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::CBV, 0, static_cast<uint32_t>(size(cbvs)), cbvs);

	// Set SRVs
	const EZ::ResourceView srvs[] =
	{
		EZ::GetSRV(m_cubeMap.get(), m_cubeMapLOD)
	};
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::SRV, 0, static_cast<uint32_t>(size(srvs)), srvs);

	// Set sampler
	const auto sampler = SamplerPreset::LINEAR_CLAMP;
	pCommandList->SetSamplerStates(Shader::Stage::PS, 0, 1, &sampler);

	pCommandList->Draw(4, 6, 0, 0);

	pCommandList->RSSetState(Graphics::CULL_BACK);
}

void FluidEZ::rayCastDirect(EZ::CommandList* pCommandList, uint8_t frameIndex)
{
	// Set pipeline state
	pCommandList->SetGraphicsShader(Shader::Stage::VS, m_shaders[VS_SCREEN_QUAD]);
	pCommandList->SetGraphicsShader(Shader::Stage::PS, m_shaders[PS_RAY_CAST]);
	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
	pCommandList->DSSetState(Graphics::DEPTH_STENCIL_NONE);
	pCommandList->OMSetBlendState(Graphics::PREMULTIPLITED);

	// Set CBVs
	const EZ::ResourceView cbvs[] =
	{
		EZ::GetCBV(m_cbPerObject.get(), frameIndex),
		EZ::GetCBV(m_cbPerFrame.get(), frameIndex),
		EZ::GetCBV(m_cbSampleRes[CB_SAMPLE_RES].get(), frameIndex)
	};
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::CBV, 0, static_cast<uint32_t>(size(cbvs)), cbvs);

	// Set SRVs
	const auto srv = EZ::GetSRV(m_colors[m_frameParity].get());
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::SRV, 0, 1, &srv);

	if (m_coeffSH)
	{
		const auto srv = EZ::GetSRV(m_coeffSH.get());
		pCommandList->SetResources(Shader::Stage::PS, DescriptorType::SRV, 1, 1, &srv);
	}

	// Set sampler
	const auto sampler = SamplerPreset::LINEAR_CLAMP;
	pCommandList->SetSamplerStates(Shader::Stage::PS, 0, 1, &sampler);

	pCommandList->Draw(3, 1, 0, 0);
}

void FluidEZ::rayCastVDirect(EZ::CommandList* pCommandList, uint8_t frameIndex)
{
	// Set pipeline state
	pCommandList->SetGraphicsShader(Shader::Stage::VS, m_shaders[VS_SCREEN_QUAD]);
	pCommandList->SetGraphicsShader(Shader::Stage::PS, m_shaders[PS_RAY_CAST_V]);
	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
	pCommandList->DSSetState(Graphics::DEPTH_STENCIL_NONE);
	pCommandList->OMSetBlendState(Graphics::PREMULTIPLITED);

	// Set CBVs
	const EZ::ResourceView cbvs[] =
	{
		EZ::GetCBV(m_cbPerObject.get(), frameIndex),
		EZ::GetCBV(m_cbPerFrame.get(), frameIndex),
		EZ::GetCBV(m_cbSampleRes[CB_SAMPLE_RES].get(), frameIndex)
	};
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::CBV, 0, static_cast<uint32_t>(size(cbvs)), cbvs);

	// Set SRVs
	const EZ::ResourceView srvs[] =
	{
		EZ::GetSRV(m_colors[m_frameParity].get()),
		EZ::GetSRV(m_lightMap.get())
	};
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::SRV, 0, static_cast<uint32_t>(size(srvs)), srvs);

	// Set sampler
	const auto sampler = SamplerPreset::LINEAR_CLAMP;
	pCommandList->SetSamplerStates(Shader::Stage::PS, 0, 1, &sampler);

	pCommandList->Draw(3, 1, 0, 0);
}
