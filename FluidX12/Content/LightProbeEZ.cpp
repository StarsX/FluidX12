//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "LightProbeEZ.h"
#include "Advanced/XUSGSHSharedConsts.h"
#define _INDEPENDENT_DDS_LOADER_
#include "Advanced/XUSGDDSLoader.h"
#undef _INDEPENDENT_DDS_LOADER_

using namespace std;
using namespace DirectX;
using namespace XUSG;

struct CBPerFrame
{
	DirectX::XMFLOAT4	EyePos;
	DirectX::XMFLOAT4X4	ScreenToWorld;
};

LightProbeEZ::LightProbeEZ()
{
	m_shaderLib = ShaderLib::MakeShared();
}

LightProbeEZ::~LightProbeEZ()
{
}

bool LightProbeEZ::Init(CommandList* pCommandList, vector<Resource::uptr>& uploaders, const wchar_t* fileName)
{
	const auto pDevice = pCommandList->GetDevice();

	// Load input image
	auto texWidth = 1u, texHeight = 1u;
	{
		DDS::Loader textureLoader;
		DDS::AlphaMode alphaMode;

		uploaders.emplace_back(Resource::MakeUnique());
		XUSG_N_RETURN(textureLoader.CreateTextureFromFile(pCommandList, fileName,
			8192, false, m_radiance, uploaders.back().get(), &alphaMode), false);

		texWidth = static_cast<uint32_t>(m_radiance->GetWidth());
		texHeight = m_radiance->GetHeight();
	}

	// Create resources
	m_numSHTexels = SH_TEX_SIZE * SH_TEX_SIZE * 6;
	const auto numGroups = XUSG_DIV_UP(m_numSHTexels, SH_GROUP_SIZE);
	const auto numSumGroups = XUSG_DIV_UP(numGroups, SH_GROUP_SIZE);
	const auto maxElements = SH_MAX_ORDER * SH_MAX_ORDER * numGroups;
	const auto maxSumElements = SH_MAX_ORDER * SH_MAX_ORDER * numSumGroups;
	m_coeffSH[0] = StructuredBuffer::MakeShared();
	XUSG_N_RETURN(m_coeffSH[0]->Create(pDevice, maxElements, sizeof(float[3]),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, MemoryFlag::NONE, L"SHCoefficients0"), false);
	m_coeffSH[1] = StructuredBuffer::MakeShared();
	XUSG_N_RETURN(m_coeffSH[1]->Create(pDevice, maxSumElements, sizeof(float[3]),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, MemoryFlag::NONE, L"SHCoefficients1"), false);
	m_weightSH[0] = StructuredBuffer::MakeUnique();
	XUSG_N_RETURN(m_weightSH[0]->Create(pDevice, numGroups, sizeof(float),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, MemoryFlag::NONE, L"SHWeights0"), false);
	m_weightSH[1] = StructuredBuffer::MakeUnique();
	XUSG_N_RETURN(m_weightSH[1]->Create(pDevice, numSumGroups, sizeof(float),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, MemoryFlag::NONE, L"SHWeights1"), false);

	// Create constant buffers
	m_cbPerFrame = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbPerFrame->Create(pDevice, sizeof(CBPerFrame[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"LightProbe.CBPerFrame"), false);

	{
		m_cbCubeMapSlices = ConstantBuffer::MakeUnique();
		XUSG_N_RETURN(m_cbCubeMapSlices->Create(pDevice, sizeof(uint32_t[6]), 6, nullptr,
			MemoryType::UPLOAD, MemoryFlag::NONE, L"Slices"), false);

		for (uint8_t i = 0; i < 6; ++i)
			*static_cast<uint32_t*>(m_cbCubeMapSlices->Map(i)) = i;
	}

	{
		m_cbSHCubeMap = ConstantBuffer::MakeUnique();
		XUSG_N_RETURN(m_cbSHCubeMap->Create(pDevice, sizeof(uint32_t[2]), 1, nullptr,
			MemoryType::UPLOAD, MemoryFlag::NONE, L"CBSHCubeMap"), false);

		static_cast<uint32_t*>(m_cbSHCubeMap->Map())[1] = SH_TEX_SIZE;
	}

	{
		auto loopCount = 0u;
		for (auto n = XUSG_DIV_UP(m_numSHTexels, SH_GROUP_SIZE); n > 1; n = XUSG_DIV_UP(n, SH_GROUP_SIZE)) ++loopCount;
		m_cbSHSums = ConstantBuffer::MakeUnique();
		XUSG_N_RETURN(m_cbSHSums->Create(pDevice, sizeof(uint32_t[2]) * loopCount, loopCount,
			nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBSHSums"), false);

		loopCount = 0;
		for (auto n = XUSG_DIV_UP(m_numSHTexels, SH_GROUP_SIZE); n > 1; n = XUSG_DIV_UP(n, SH_GROUP_SIZE))
			static_cast<uint32_t*>(m_cbSHSums->Map(loopCount++))[1] = n;
	}

	// Create shaders
	return createShaders();
}

void LightProbeEZ::UpdateFrame(uint8_t frameIndex, CXMMATRIX viewProj, const XMFLOAT3& eyePt)
{
	const auto projToWorld = XMMatrixInverse(nullptr, viewProj);
	const auto pCbData = static_cast<CBPerFrame*>(m_cbPerFrame->Map(frameIndex));
	pCbData->EyePos = XMFLOAT4(eyePt.x, eyePt.y, eyePt.z, 1.0f);
	XMStoreFloat4x4(&pCbData->ScreenToWorld, XMMatrixTranspose(projToWorld));
}

void LightProbeEZ::TransformSH(EZ::CommandList* pCommandList)
{
	const uint8_t order = 3;
	shCubeMap(pCommandList, order);
	shSum(pCommandList, order);
	shNormalize(pCommandList, order);
}

void LightProbeEZ::RenderEnvironment(EZ::CommandList* pCommandList, uint8_t frameIndex)
{
	// Set pipeline state
	pCommandList->SetGraphicsShader(Shader::Stage::VS, m_shaders[VS_SCREEN_QUAD]);
	pCommandList->SetGraphicsShader(Shader::Stage::PS, m_shaders[PS_ENVIRONMENT]);
	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
	pCommandList->DSSetState(Graphics::DEPTH_READ_LESS_EQUAL);
	pCommandList->OMSetBlendState(Graphics::DEFAULT_OPAQUE);

	// Set CBV
	const auto cbv = EZ::GetCBV(m_cbPerFrame.get(), frameIndex);
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::CBV, 0, 1, &cbv);

	// Set SRV
	const auto srv = EZ::GetSRV(m_radiance.get());
	pCommandList->SetResources(Shader::Stage::PS, DescriptorType::SRV, 0, 1, &srv);

	// Set sampler
	const auto sampler = SamplerPreset::ANISOTROPIC_WRAP;
	pCommandList->SetSamplerStates(Shader::Stage::PS, 0, 1, &sampler);

	pCommandList->Draw(3, 1, 0, 0);
}

Texture::sptr LightProbeEZ::GetRadiance() const
{
	return m_radiance;
}

StructuredBuffer::sptr LightProbeEZ::GetSH() const
{
	return m_coeffSH[m_shBufferParity];
}

bool LightProbeEZ::createShaders()
{
	auto vsIndex = 0u;
	auto psIndex = 0u;
	auto csIndex = 0u;

	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSScreenQuad.cso"), false);
	m_shaders[VS_SCREEN_QUAD] = m_shaderLib->GetShader(Shader::Stage::VS, vsIndex++);

	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, psIndex, L"PSEnvironment.cso"), false);
	m_shaders[PS_ENVIRONMENT] = m_shaderLib->GetShader(Shader::Stage::PS, psIndex++);

	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSSHCubeMap.cso"), false);
	m_shaders[CS_SH_CUBE_MAP] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);

	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSSHSum.cso"), false);
	m_shaders[CS_SH_SUM] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);

	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSSHNormalize.cso"), false);
	m_shaders[CS_SH_NORMALIZE] = m_shaderLib->GetShader(Shader::Stage::CS, csIndex++);

	return true;
}

void LightProbeEZ::shCubeMap(EZ::CommandList* pCommandList, uint8_t order)
{
	// Set pipeline state
	pCommandList->SetComputeShader(m_shaders[CS_SH_CUBE_MAP]);

	// Set UAVs
	const EZ::ResourceView uavs[] =
	{
		EZ::GetUAV(m_coeffSH[0].get()),
		EZ::GetUAV(m_weightSH[0].get())

	};
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::UAV, 0, static_cast<uint32_t>(size(uavs)), uavs);

	// Set CBV
	assert(order <= SH_MAX_ORDER);
	*static_cast<uint32_t*>(m_cbSHCubeMap->Map()) = order;
	const auto cbv = EZ::GetCBV(m_cbSHCubeMap.get());
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, 0, 1, &cbv);

	// Set SRV
	const auto srv = EZ::GetSRV(m_radiance.get());
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 0, 1, &srv);

	const auto sampler = SamplerPreset::LINEAR_WRAP;
	pCommandList->SetSamplerStates(Shader::Stage::CS, 0, 1, &sampler);

	pCommandList->Dispatch(XUSG_DIV_UP(m_numSHTexels, SH_GROUP_SIZE), 1, 1);
}

void LightProbeEZ::shSum(EZ::CommandList* pCommandList, uint8_t order)
{
	assert(order <= SH_MAX_ORDER);
	m_shBufferParity = 0;

	// Set pipeline state
	pCommandList->SetComputeShader(m_shaders[CS_SH_SUM]);

	auto i = 0u;
	for (auto n = XUSG_DIV_UP(m_numSHTexels, SH_GROUP_SIZE); n > 1; n = XUSG_DIV_UP(n, SH_GROUP_SIZE))
	{
		const auto& src = m_shBufferParity;
		const uint8_t dst = !m_shBufferParity;

		// Set UAVs
		const EZ::ResourceView uavs[] =
		{
			EZ::GetUAV(m_coeffSH[dst].get()),
			EZ::GetUAV(m_weightSH[dst].get())

		};
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::UAV, 0, static_cast<uint32_t>(size(uavs)), uavs);

		// Set SRVs
		const EZ::ResourceView srvs[] =
		{
			EZ::GetSRV(m_coeffSH[src].get()),
			EZ::GetSRV(m_weightSH[src].get())
		};
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 0, static_cast<uint32_t>(size(srvs)), srvs);

		// Set CBV
		*static_cast<uint32_t*>(m_cbSHSums->Map(i++)) = order;
		const auto cbv = EZ::GetCBV(m_cbSHSums.get());
		pCommandList->SetResources(Shader::Stage::CS, DescriptorType::CBV, 0, 1, &cbv);

		pCommandList->Dispatch(XUSG_DIV_UP(n, SH_GROUP_SIZE), order * order, 1);
		m_shBufferParity = !m_shBufferParity;
	}
}

void LightProbeEZ::shNormalize(EZ::CommandList* pCommandList, uint8_t order)
{
	assert(order <= SH_MAX_ORDER);
	const auto& src = m_shBufferParity;
	const uint8_t dst = !m_shBufferParity;

	// Set pipeline state
	pCommandList->SetComputeShader(m_shaders[CS_SH_NORMALIZE]);

	// Set UAV
	const auto uav = EZ::GetUAV(m_coeffSH[dst].get());
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::UAV, 0, 1, &uav);

	// Set SRVs
	const EZ::ResourceView srvs[] =
	{
		EZ::GetSRV(m_coeffSH[src].get()),
		EZ::GetSRV(m_weightSH[src].get())
	};
	pCommandList->SetResources(Shader::Stage::CS, DescriptorType::SRV, 0, static_cast<uint32_t>(size(srvs)), srvs);

	const auto numElements = order * order;
	pCommandList->Dispatch(XUSG_DIV_UP(numElements, SH_GROUP_SIZE), 1, 1);
	m_shBufferParity = !m_shBufferParity;
}
