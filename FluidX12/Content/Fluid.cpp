//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Fluid.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

struct CBPerObject
{
	XMVECTOR LocalSpaceLightPt;
	XMVECTOR LocalSpaceEyePt;
	XMMATRIX ScreenToLocal;
	XMMATRIX WorldViewProj;
};

Fluid::Fluid(const Device& device) :
	m_device(device),
	m_timeInterval(0.0f),
	m_frameParity(0)
{
	m_shaderPool = ShaderPool::MakeUnique();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeUnique(device);
	m_computePipelineCache = Compute::PipelineCache::MakeUnique(device);
	m_pipelineLayoutCache = PipelineLayoutCache::MakeUnique(device);
}

Fluid::~Fluid()
{
}

bool Fluid::Init(CommandList* pCommandList, uint32_t width, uint32_t height,
	shared_ptr<DescriptorTableCache> descriptorTableCache, vector<Resource>& uploaders,
	Format rtFormat, Format dsFormat, const XMUINT3& gridSize, uint32_t numParticles)
{
	m_viewport = XMUINT2(width, height);
	m_descriptorTableCache = descriptorTableCache;
	m_gridSize = gridSize;
	m_numParticles = numParticles;

	// Create resources
	for (uint8_t i = 0; i < 2; ++i)
	{
		m_velocities[i] = Texture3D::MakeUnique();
		N_RETURN(m_velocities[i]->Create(m_device, gridSize.x, gridSize.y, gridSize.z, Format::R16G16B16A16_FLOAT,
			i ? ResourceFlag::ALLOW_UNORDERED_ACCESS : (ResourceFlag::ALLOW_UNORDERED_ACCESS |
				ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS), 1, MemoryType::DEFAULT,
				(L"Velocity" + to_wstring(i)).c_str()), false);

		m_colors[i] = Texture3D::MakeUnique();
		N_RETURN(m_colors[i]->Create(m_device, gridSize.x, gridSize.y, gridSize.z, Format::R16G16B16A16_FLOAT,
			ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryType::DEFAULT,
			(L"Color" + to_wstring(i)).c_str()), false);
	}

	m_incompress = Texture3D::MakeUnique();
	N_RETURN(m_incompress->Create(m_device, gridSize.x, gridSize.y, gridSize.z, Format::R32_FLOAT,
		ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryType::DEFAULT,
		L"Incompressibility"), false);

	ResourceBarrier barrier;
	const auto numBarriers = m_incompress->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	pCommandList->Barrier(numBarriers, &barrier);

	if (numParticles > 0)
	{
		m_particleBuffer = StructuredBuffer::MakeUnique();
		N_RETURN(m_particleBuffer->Create(m_device, numParticles, sizeof(ParticleInfo),
			ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, 1, nullptr, 1,
			nullptr, L"ParticleBuffer"), false);

		vector<ParticleInfo> particles(numParticles);
		for (auto& particle : particles)
		{
			particle = {};
			particle.Pos.y = FLT_MAX;
			particle.LifeTime = rand() % numParticles / 10000.0f;
		}
		uploaders.emplace_back();
		m_particleBuffer->Upload(pCommandList, uploaders.back(), particles.data(),
			sizeof(ParticleInfo) * numParticles);
	}

	// Create pipelines
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(rtFormat, dsFormat), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void Fluid::UpdateFrame(float timeStep, const XMFLOAT4X4& view, const XMFLOAT4X4& proj, const XMFLOAT3& eyePt)
{
	m_timeStep = timeStep;
	if (timeStep > 0.0) m_frameParity = !m_frameParity;
	m_view = view;
	m_proj = proj;
	m_eyePt = eyePt;
}

void Fluid::Simulate(const CommandList* pCommandList)
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
		pCommandList->SetCompute32BitConstant(0, reinterpret_cast<uint32_t&>(timeStep));
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
		pCommandList->SetCompute32BitConstant(0, reinterpret_cast<uint32_t&>(timeStep));
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

void Fluid::Render(const CommandList* pCommandList)
{
	if (m_numParticles > 0) renderParticles(pCommandList);
	else if (m_gridSize.z > 1) rayCast(pCommandList);
	else visualizeColor(pCommandList);
}

bool Fluid::createPipelineLayouts()
{
	// Advection
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(float), 0);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(1, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetRange(2, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout->SetRange(3, DescriptorType::SRV, 1, 1);
		pipelineLayout->SetRange(3, DescriptorType::UAV, 1, 1, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[ADVECT], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"AdvectionLayout"), false);
	}

	// Projection
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(float), 0);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(1, DescriptorType::UAV, 2, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[PROJECT], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ProjectionLayout"), false);
	}

	if (m_numParticles > 0)
	{
		// Particle rendering
		struct CBPerFrame
		{
			float TimeStep;
			uint32_t BaseSeed;
		};
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(CBPerFrame), 0, 0, Shader::Stage::VS);
		pipelineLayout->SetConstants(1, SizeOfInUint32(XMMATRIX), 1, 0, Shader::Stage::VS);
		pipelineLayout->SetConstants(2, SizeOfInUint32(XMMATRIX[2]), 0, 0, Shader::Stage::DS);
		pipelineLayout->SetRange(3, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetRange(3, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(4, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(5, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout->SetShaderStage(2, Shader::Stage::DS);
		pipelineLayout->SetShaderStage(3, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(4, Shader::Stage::DS);
		X_RETURN(m_pipelineLayouts[VISUALIZE], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ParticleLayout"), false);
	}
	else if (m_gridSize.z > 1)
	{
		// Ray casting
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(CBPerObject), 0, 0, Shader::Stage::PS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(2, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout->SetShaderStage(0, Shader::Stage::PS);
		pipelineLayout->SetShaderStage(1, Shader::Stage::PS);
		pipelineLayout->SetShaderStage(2, Shader::Stage::PS);
		X_RETURN(m_pipelineLayouts[VISUALIZE], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"RayCastingLayout"), false);
	}
	else
	{
		// Visualization
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRange(0, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(1, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout->SetShaderStage(0, Shader::PS);
		pipelineLayout->SetShaderStage(1, Shader::PS);
		X_RETURN(m_pipelineLayouts[VISUALIZE], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"VisualizationLayout"), false);
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
		X_RETURN(m_pipelines[ADVECT], state->GetPipeline(*m_computePipelineCache, L"Advection"), false);
	}

	// Projection
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, m_gridSize.z > 1 ?
			L"CSProject3D.cso" : L"CSProject2D.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PROJECT]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[PROJECT], state->GetPipeline(*m_computePipelineCache, L"Projection"), false);
	}

	// Visualization
	if (m_numParticles > 0)
	{
		// Particle rendering
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSParticle.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::HS, hsIndex, L"HSParticle.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::DS, dsIndex, L"DSParticle.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSParticle.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[VISUALIZE]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::HS, m_shaderPool->GetShader(Shader::Stage::HS, hsIndex++));
		state->SetShader(Shader::Stage::DS, m_shaderPool->GetShader(Shader::Stage::DS, dsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::PATCH);
		state->DSSetState(Graphics::DEPTH_STENCIL_NONE, *m_graphicsPipelineCache);
		state->OMSetBlendState(Graphics::NON_PRE_MUL, *m_graphicsPipelineCache);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		//state->OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[VISUALIZE], state->GetPipeline(*m_graphicsPipelineCache, L"Particle"), false);
	}
	else if (m_gridSize.z > 1)
	{
		// Ray casting
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSScreenQuad.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSRayCast.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[VISUALIZE]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
		state->DSSetState(Graphics::DEPTH_STENCIL_NONE, *m_graphicsPipelineCache);
		state->OMSetBlendState(Graphics::NON_PRE_MUL, *m_graphicsPipelineCache);
		state->OMSetRTVFormats(&rtFormat, 1);
		X_RETURN(m_pipelines[VISUALIZE], state->GetPipeline(*m_graphicsPipelineCache, L"RayCasting"), false);
	}
	else
	{
		// Visualization
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSScreenQuad.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSVisualizeColor.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[VISUALIZE]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex++));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
		state->OMSetBlendState(Graphics::NON_PRE_MUL, *m_graphicsPipelineCache);
		state->DSSetState(Graphics::DEPTH_STENCIL_NONE, *m_graphicsPipelineCache);
		state->OMSetRTVFormats(&rtFormat, 1);
		X_RETURN(m_pipelines[VISUALIZE], state->GetPipeline(*m_graphicsPipelineCache, L"Visualization"), false);
	}

	return true;
}

bool Fluid::createDescriptorTables()
{
	if (m_numParticles > 0)
	{
		// Create particle UAV
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_particleBuffer->GetUAV());
		X_RETURN(m_srvUavTables[UAV_SRV_TABLE_PARTICLE], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Create SRV and UAV tables
	for (uint8_t i = 0; i < 2; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_velocities[i]->GetSRV(),
			m_velocities[(i + 1) % 2]->GetUAV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvUavTables[SRV_UAV_TABLE_VECOLITY + i], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		// Create incompressibility UAV
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_incompress->GetUAV());
		X_RETURN(m_srvUavTables[UAV_TABLE_INCOMPRESS], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	for (uint8_t i = 0; i < 2; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_colors[(i + 1) % 2]->GetSRV(),
			m_colors[i]->GetUAV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvUavTables[SRV_UAV_TABLE_COLOR + i], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Create the samplers
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const auto samplerLinearMirror = SamplerPreset::LINEAR_MIRROR;
		descriptorTable->SetSamplers(0, 1, &samplerLinearMirror, *m_descriptorTableCache);
		X_RETURN(m_samplerTables[SAMPLER_TABLE_MIRROR], descriptorTable->GetSamplerTable(*m_descriptorTableCache), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const auto samplerLinearClamp = SamplerPreset::LINEAR_CLAMP;
		descriptorTable->SetSamplers(0, 1, &samplerLinearClamp, *m_descriptorTableCache);
		X_RETURN(m_samplerTables[SAMPLER_TABLE_CLAMP], descriptorTable->GetSamplerTable(*m_descriptorTableCache), false);
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

void Fluid::rayCast(const CommandList* pCommandList)
{
	// General matrices
	const auto world = getWorldMatrix();
	const auto worldViewProj = world * XMLoadFloat4x4(&m_view) * XMLoadFloat4x4(&m_proj);
	const auto worldI = XMMatrixInverse(nullptr, world);

	// Screen space matrices
	CBPerObject cbPerObject;
	cbPerObject.LocalSpaceLightPt = XMVector3TransformCoord(XMVectorSet(75.0f, 75.0f, -75.0f, 0.0f), worldI);
	cbPerObject.LocalSpaceEyePt = XMVector3TransformCoord(XMLoadFloat3(&m_eyePt), worldI);

	const auto mToScreen = XMMATRIX
	(
		0.5f * m_viewport.x, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f * m_viewport.y, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f * m_viewport.x, 0.5f * m_viewport.y, 0.0f, 1.0f
	);
	const auto localToScreen = XMMatrixMultiply(worldViewProj, mToScreen);
	const auto screenToLocal = XMMatrixInverse(nullptr, localToScreen);
	cbPerObject.ScreenToLocal = XMMatrixTranspose(screenToLocal);
	cbPerObject.WorldViewProj = XMMatrixTranspose(worldViewProj);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[VISUALIZE]);
	pCommandList->SetPipelineState(m_pipelines[VISUALIZE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);

	// Set descriptor tables
	pCommandList->SetGraphics32BitConstants(0, SizeOfInUint32(cbPerObject), &cbPerObject);
	pCommandList->SetGraphicsDescriptorTable(1, m_srvUavTables[SRV_UAV_TABLE_COLOR + !m_frameParity]);
	pCommandList->SetGraphicsDescriptorTable(2, m_samplerTables[SAMPLER_TABLE_CLAMP]);

	pCommandList->Draw(3, 1, 0, 0);
}

void Fluid::renderParticles(const CommandList* pCommandList)
{
	// General matrices
	XMMATRIX worldView, worldViewI, proj;
	if (m_gridSize.z > 1)
	{
		const auto world = getWorldMatrix();
		worldView = world * XMLoadFloat4x4(&m_view);
		proj = XMMatrixTranspose(XMLoadFloat4x4(&m_proj));

	}
	else
	{
		worldView = getWorldMatrix();
		proj = XMMatrixScaling(0.1f, 0.1f, 0.1f);
	}
	worldViewI = XMMatrixTranspose(XMMatrixInverse(nullptr, worldView));
	worldView = XMMatrixTranspose(worldView);

	// Set barrier
	ResourceBarrier barrier;
	const auto numBarriers = m_velocities[0]->SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);;
	pCommandList->Barrier(numBarriers, &barrier);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[VISUALIZE]);
	pCommandList->SetPipelineState(m_pipelines[VISUALIZE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::CONTROL_POINT1_PATCHLIST);

	// Set descriptor tables
	pCommandList->SetGraphics32BitConstant(0, reinterpret_cast<uint32_t&>(m_timeStep));
	pCommandList->SetGraphics32BitConstant(0, rand(), SizeOfInUint32(m_timeStep));
	pCommandList->SetGraphics32BitConstants(1, SizeOfInUint32(worldView), &worldView);
	pCommandList->SetGraphics32BitConstants(2, SizeOfInUint32(worldViewI), &worldViewI);
	pCommandList->SetGraphics32BitConstants(2, SizeOfInUint32(proj), &proj, SizeOfInUint32(worldViewI));
	pCommandList->SetGraphicsDescriptorTable(3, m_srvUavTables[UAV_SRV_TABLE_PARTICLE]);
	pCommandList->SetGraphicsDescriptorTable(4, m_srvUavTables[SRV_UAV_TABLE_COLOR + !m_frameParity]);
	pCommandList->SetGraphicsDescriptorTable(5, m_samplerTables[SAMPLER_TABLE_CLAMP]);

	pCommandList->Draw(m_numParticles, 1, 0, 0);
}

XMMATRIX Fluid::getWorldMatrix() const
{
	return XMMatrixScaling(10.0f, 10.0f, 10.0f);
}
