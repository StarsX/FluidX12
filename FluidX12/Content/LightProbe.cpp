//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "LightProbe.h"
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

LightProbe::LightProbe()
{
	m_shaderPool = ShaderPool::MakeShared();
}

LightProbe::~LightProbe()
{
}

bool LightProbe::Init(CommandList* pCommandList, const DescriptorTableCache::sptr& descriptorTableCache,
	vector<Resource::uptr>& uploaders, const wchar_t* fileName, Format rtFormat, Format dsFormat)
{
	const auto pDevice = pCommandList->GetDevice();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeUnique(pDevice);
	m_computePipelineCache = Compute::PipelineCache::MakeShared(pDevice);
	m_pipelineLayoutCache = PipelineLayoutCache::MakeShared(pDevice);
	m_descriptorTableCache = descriptorTableCache;

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

	// Create resources and pipelines
	m_cbPerFrame = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbPerFrame->Create(pDevice, sizeof(CBPerFrame[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"LightProbe.CBPerFrame"), false);

	XUSG_N_RETURN(createPipelineLayouts(), false);
	XUSG_N_RETURN(createPipelines(rtFormat, dsFormat), false);

	return true;
}

bool LightProbe::CreateDescriptorTables(Device* pDevice)
{
	m_sphericalHarmonics = SphericalHarmonics::MakeUnique();
	XUSG_N_RETURN(m_sphericalHarmonics->Init(pDevice, m_shaderPool, m_computePipelineCache,
		m_pipelineLayoutCache, m_descriptorTableCache, 0), false);

	return createDescriptorTables();
}

void LightProbe::UpdateFrame(uint8_t frameIndex, CXMMATRIX viewProj, const XMFLOAT3& eyePt)
{
	const auto projToWorld = XMMatrixInverse(nullptr, viewProj);
	const auto pCbData = reinterpret_cast<CBPerFrame*>(m_cbPerFrame->Map(frameIndex));
	pCbData->EyePos = XMFLOAT4(eyePt.x, eyePt.y, eyePt.z, 1.0f);
	XMStoreFloat4x4(&pCbData->ScreenToWorld, XMMatrixTranspose(projToWorld));
}

void LightProbe::TransformSH(CommandList* pCommandList)
{
	m_sphericalHarmonics->Transform(pCommandList, m_radiance.get(), m_srvTable);
}

void LightProbe::RenderEnvironment(const CommandList* pCommandList, uint8_t frameIndex)
{
	// Set descriptor tables
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[ENVIRONMENT]);
	pCommandList->SetGraphicsRootConstantBufferView(0, m_cbPerFrame.get(), m_cbPerFrame->GetCBVOffset(frameIndex));
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);

	// Set pipeline state
	pCommandList->SetPipelineState(m_pipelines[ENVIRONMENT]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
	pCommandList->Draw(3, 1, 0, 0);
}

ShaderResource* LightProbe::GetRadiance() const
{
	return m_radiance.get();
}

StructuredBuffer::sptr LightProbe::GetSH() const
{
	return m_sphericalHarmonics->GetSHCoefficients();
}

bool LightProbe::createPipelineLayouts()
{
	// Environment mapping
	{
		const auto sampler = m_descriptorTableCache->GetSampler(SamplerPreset::LINEAR_WRAP);

		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::PS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetShaderStage(1, Shader::Stage::PS);
		pipelineLayout->SetStaticSamplers(&sampler, 1, 0, 0, Shader::PS);
		XUSG_X_RETURN(m_pipelineLayouts[ENVIRONMENT], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"EnvironmentLayout"), false);
	}

	return true;
}

bool LightProbe::createPipelines(Format rtFormat, Format dsFormat)
{
	// Environment mapping
	{
		XUSG_N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, ENVIRONMENT, L"VSScreenQuad.cso"), false);
		XUSG_N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, ENVIRONMENT, L"PSEnvironment.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[ENVIRONMENT]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, ENVIRONMENT));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, ENVIRONMENT));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
		state->DSSetState(Graphics::DEPTH_READ_LESS_EQUAL, m_graphicsPipelineCache.get());
		state->OMSetRTVFormats(&rtFormat, 1);
		state->OMSetDSVFormat(dsFormat);
		XUSG_X_RETURN(m_pipelines[ENVIRONMENT], state->GetPipeline(m_graphicsPipelineCache.get(), L"Environment"), false);
	}

	return true;
}

bool LightProbe::createDescriptorTables()
{
	// Get SRV for cube-map input
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_radiance->GetSRV());
		XUSG_X_RETURN(m_srvTable, descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	return true;
}
