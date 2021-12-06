//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "LightProbe.h"
#define _INDEPENDENT_DDS_LOADER_
#include "Advanced/XUSGDDSLoader.h"
#undef _INDEPENDENT_DDS_LOADER_

#define SH_MAX_ORDER	6
#define SH_GROUP_SIZE	32

using namespace std;
using namespace DirectX;
using namespace XUSG;

struct CBPerFrame
{
	DirectX::XMFLOAT4	EyePos;
	DirectX::XMFLOAT4X4	ScreenToWorld;
};

LightProbe::LightProbe(const Device::sptr& device) :
	m_device(device)
{
	m_shaderPool = ShaderPool::MakeUnique();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeUnique(device.get());
	m_computePipelineCache = Compute::PipelineCache::MakeUnique(device.get());
	m_pipelineLayoutCache = PipelineLayoutCache::MakeUnique(device.get());
}

LightProbe::~LightProbe()
{
}

bool LightProbe::Init(CommandList* pCommandList, const DescriptorTableCache::sptr& descriptorTableCache,
	vector<Resource::uptr>& uploaders, const wchar_t* fileName, Format rtFormat, Format dsFormat)
{
	m_descriptorTableCache = descriptorTableCache;

	// Load input image
	auto texWidth = 1u, texHeight = 1u;
	{
		DDS::Loader textureLoader;
		DDS::AlphaMode alphaMode;

		uploaders.emplace_back(Resource::MakeUnique());
		N_RETURN(textureLoader.CreateTextureFromFile(m_device.get(), pCommandList, fileName,
			8192, false, m_radiance, uploaders.back().get(), &alphaMode), false);

		texWidth = m_radiance->GetWidth();
		texHeight = dynamic_pointer_cast<Texture2D, ShaderResource>(m_radiance)->GetHeight();
	}

	// Create resources and pipelines
	m_numSHTexels = texWidth * texHeight * 6;
	const auto numGroups = DIV_UP(m_numSHTexels, SH_GROUP_SIZE);
	const auto numSumGroups = DIV_UP(numGroups, SH_GROUP_SIZE);
	const auto maxElements = SH_MAX_ORDER * SH_MAX_ORDER * numGroups;
	const auto maxSumElements = SH_MAX_ORDER * SH_MAX_ORDER * numSumGroups;
	m_coeffSH[0] = StructuredBuffer::MakeShared();
	m_coeffSH[0]->Create(m_device.get(), maxElements, sizeof(float[3]),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, MemoryFlag::NONE, L"SHCoefficients0");
	m_coeffSH[1] = StructuredBuffer::MakeShared();
	m_coeffSH[1]->Create(m_device.get(), maxSumElements, sizeof(float[3]),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, MemoryFlag::NONE, L"SHCoefficients1");
	m_weightSH[0] = StructuredBuffer::MakeUnique();
	m_weightSH[0]->Create(m_device.get(), numGroups, sizeof(float),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, MemoryFlag::NONE, L"SHWeights0");
	m_weightSH[1] = StructuredBuffer::MakeUnique();
	m_weightSH[1]->Create(m_device.get(), numSumGroups, sizeof(float),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, MemoryFlag::NONE, L"SHWeights1");

	m_cbPerFrame = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbPerFrame->Create(m_device.get(), sizeof(CBPerFrame[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"LightProbe.CBPerFrame"), false);

	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(rtFormat, dsFormat), false);

	return true;
}

bool LightProbe::CreateDescriptorTables()
{
	return createDescriptorTables();
}

void LightProbe::UpdateFrame(uint8_t frameIndex, CXMMATRIX viewProj, const XMFLOAT3& eyePt)
{
	const auto projToWorld = XMMatrixInverse(nullptr, viewProj);
	const auto pCbData = reinterpret_cast<CBPerFrame*>(m_cbPerFrame->Map(frameIndex));
	pCbData->EyePos = XMFLOAT4(eyePt.x, eyePt.y, eyePt.z, 1.0f);
	XMStoreFloat4x4(&pCbData->ScreenToWorld, XMMatrixTranspose(projToWorld));
}

void LightProbe::Process(CommandList* pCommandList, uint8_t frameIndex)
{
	// Set Descriptor pools
	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
		m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	const uint8_t order = 3;
	shCubeMap(pCommandList, order);
	shSum(pCommandList, order);
	shNormalize(pCommandList, order);
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
	return m_coeffSH[m_shBufferParity];
}

bool LightProbe::createPipelineLayouts()
{
	// SH cube map transform
	{
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();
		utilPipelineLayout->SetRange(0, DescriptorType::SAMPLER, 1, 0);
		utilPipelineLayout->SetRootUAV(1, 0);
		utilPipelineLayout->SetRootUAV(2, 1);
		utilPipelineLayout->SetRange(3, DescriptorType::SRV, 1, 0);
		utilPipelineLayout->SetConstants(4, SizeOfInUint32(uint32_t[2]), 0);
		X_RETURN(m_pipelineLayouts[SH_CUBE_MAP], utilPipelineLayout->GetPipelineLayout(
			m_pipelineLayoutCache.get(), PipelineLayoutFlag::NONE, L"SHCubeMapLayout"), false);
	}

	// SH sum
	{
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();
		utilPipelineLayout->SetRootUAV(0, 0);
		utilPipelineLayout->SetRootUAV(1, 1);
		utilPipelineLayout->SetRootSRV(2, 0);
		utilPipelineLayout->SetRootSRV(3, 1);
		utilPipelineLayout->SetConstants(4, SizeOfInUint32(uint32_t[2]), 0);
		X_RETURN(m_pipelineLayouts[SH_SUM], utilPipelineLayout->GetPipelineLayout(
			m_pipelineLayoutCache.get(), PipelineLayoutFlag::NONE, L"SHSumLayout"), false);
	}

	// SH normalize
	{
		const auto utilPipelineLayout = Util::PipelineLayout::MakeUnique();
		utilPipelineLayout->SetRootUAV(0, 0);
		utilPipelineLayout->SetRootSRV(1, 0);
		utilPipelineLayout->SetRootSRV(2, 1);
		X_RETURN(m_pipelineLayouts[SH_NORMALIZE], utilPipelineLayout->GetPipelineLayout(
			m_pipelineLayoutCache.get(), PipelineLayoutFlag::NONE, L"SHNormalizeLayout"), false);
	}

	// Environment mapping
	{
		const auto sampler = m_descriptorTableCache->GetSampler(SamplerPreset::LINEAR_WRAP);

		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::PS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetShaderStage(1, Shader::Stage::PS);
		pipelineLayout->SetStaticSamplers(&sampler, 1, 0, 0, Shader::PS);
		X_RETURN(m_pipelineLayouts[ENVIRONMENT], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"EnvironmentLayout"), false);
	}

	return true;
}

bool LightProbe::createPipelines(Format rtFormat, Format dsFormat)
{
	auto vsIndex = 0u;
	auto psIndex = 0u;
	auto csIndex = 0u;

	// SH cube map transform
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSSHCubeMap.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[SH_CUBE_MAP]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[SH_CUBE_MAP], state->GetPipeline(m_computePipelineCache.get(), L"SHCubeMap"), false);
	}

	// SH sum
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSSHSum.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[SH_SUM]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[SH_SUM], state->GetPipeline(m_computePipelineCache.get(), L"SHSum"), false);
	}

	// SH sum
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSSHNormalize.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[SH_NORMALIZE]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex));
		X_RETURN(m_pipelines[SH_NORMALIZE], state->GetPipeline(m_computePipelineCache.get(), L"SHNormalize"), false);
	}

	// Environment mapping
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSScreenQuad.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSEnvironment.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[ENVIRONMENT]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex++));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
		state->DSSetState(Graphics::DEPTH_READ_LESS_EQUAL, m_graphicsPipelineCache.get());
		state->OMSetRTVFormats(&rtFormat, 1);
		state->OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[ENVIRONMENT], state->GetPipeline(m_graphicsPipelineCache.get(), L"Environment"), false);
	}

	return true;
}

bool LightProbe::createDescriptorTables()
{
	// Create the sampler table
	const auto descriptorTable = Util::DescriptorTable::MakeUnique();
	const auto sampler = LINEAR_WRAP;
	descriptorTable->SetSamplers(0, 1, &sampler, m_descriptorTableCache.get());
	X_RETURN(m_samplerTable, descriptorTable->GetSamplerTable(m_descriptorTableCache.get()), false);

	// Get SRV for cube-map input
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_radiance->GetSRV());
		X_RETURN(m_srvTable, descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	return true;
}

void LightProbe::shCubeMap(CommandList* pCommandList, uint8_t order)
{
	assert(order <= SH_MAX_ORDER);
	ResourceBarrier barrier;
	m_coeffSH[0]->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);	// Promotion
	m_weightSH[0]->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);	// Promotion

	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[SH_CUBE_MAP]);
	pCommandList->SetComputeDescriptorTable(0, m_samplerTable);
	pCommandList->SetComputeRootUnorderedAccessView(1, m_coeffSH[0].get());
	pCommandList->SetComputeRootUnorderedAccessView(2, m_weightSH[0].get());
	pCommandList->SetComputeDescriptorTable(3, m_srvTable);
	pCommandList->SetCompute32BitConstant(4, order);
	pCommandList->SetCompute32BitConstant(4, m_radiance->GetWidth(), SizeOfInUint32(order));
	pCommandList->SetPipelineState(m_pipelines[SH_CUBE_MAP]);

	pCommandList->Dispatch(DIV_UP(m_numSHTexels, SH_GROUP_SIZE), 1, 1);
}

void LightProbe::shSum(CommandList* pCommandList, uint8_t order)
{
	assert(order <= SH_MAX_ORDER);
	ResourceBarrier barriers[4];
	m_shBufferParity = 0;

	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[SH_SUM]);
	pCommandList->SetCompute32BitConstant(4, order);
	pCommandList->SetPipelineState(m_pipelines[SH_SUM]);

	// Promotions
	m_coeffSH[1]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	m_weightSH[1]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);

	for (auto n = DIV_UP(m_numSHTexels, SH_GROUP_SIZE); n > 1; n = DIV_UP(n, SH_GROUP_SIZE))
	{
		const auto& src = m_shBufferParity;
		const uint8_t dst = !m_shBufferParity;
		auto numBarriers = m_coeffSH[dst]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
		numBarriers = m_weightSH[dst]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
		numBarriers = m_coeffSH[src]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
		numBarriers = m_weightSH[src]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
		pCommandList->Barrier(numBarriers, barriers);

		pCommandList->SetComputeRootUnorderedAccessView(0, m_coeffSH[dst].get());
		pCommandList->SetComputeRootUnorderedAccessView(1, m_weightSH[dst].get());
		pCommandList->SetComputeRootShaderResourceView(2, m_coeffSH[src].get());
		pCommandList->SetComputeRootShaderResourceView(3, m_weightSH[src].get());
		pCommandList->SetCompute32BitConstant(4, n, SizeOfInUint32(order));

		pCommandList->Dispatch(DIV_UP(n, SH_GROUP_SIZE), order * order, 1);
		m_shBufferParity = !m_shBufferParity;
	}
}

void LightProbe::shNormalize(CommandList* pCommandList, uint8_t order)
{
	assert(order <= SH_MAX_ORDER);
	ResourceBarrier barriers[3];
	const auto& src = m_shBufferParity;
	const uint8_t dst = !m_shBufferParity;
	auto numBarriers = m_coeffSH[dst]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_coeffSH[src]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	numBarriers = m_weightSH[src]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	pCommandList->Barrier(numBarriers, barriers);

	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[SH_NORMALIZE]);
	pCommandList->SetComputeRootUnorderedAccessView(0, m_coeffSH[dst].get());
	pCommandList->SetComputeRootShaderResourceView(1, m_coeffSH[src].get());
	pCommandList->SetComputeRootShaderResourceView(2, m_weightSH[src].get());
	pCommandList->SetPipelineState(m_pipelines[SH_NORMALIZE]);

	const auto numElements = order * order;
	pCommandList->Dispatch(DIV_UP(numElements, SH_GROUP_SIZE), 1, 1);
	m_shBufferParity = !m_shBufferParity;
}
