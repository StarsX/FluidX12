//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "DXFramework.h"
#include "Core/XUSG.h"

class Fluid
{
public:
	Fluid(const XUSG::Device& device);
	virtual ~Fluid();

	bool Init(const XUSG::CommandList& commandList, uint32_t width, uint32_t height,
		std::shared_ptr<XUSG::DescriptorTableCache> descriptorTableCache,
		std::vector<XUSG::Resource>& uploaders, XUSG::Format rtFormat,
		const DirectX::XMUINT3& gridSize, uint32_t numParticles = 0);

	void UpdateFrame(float timeStep, DirectX::CXMMATRIX viewProj, const DirectX::XMFLOAT3& eyePt);
	void Simulate(const XUSG::CommandList& commandList);
	void Render(const XUSG::CommandList& commandList);

protected:
	enum PipelineIndex : uint8_t
	{
		ADVECT,
		PROJECT,
		VISUALIZE,

		NUM_PIPELINE
	};

	enum SrvUavTable : uint8_t
	{
		SRV_UAV_TABLE_VECOLITY,
		SRV_UAV_TABLE_VECOLITY1,
		SRV_UAV_TABLE_COLOR,
		SRV_UAV_TABLE_COLOR1,
		UAV_TABLE_INCOMPRESS,
		UAV_SRV_TABLE_PARTICLE,

		NUM_SRV_UAV_TABLE
	};
	
	enum SamplerTable : uint8_t
	{
		SAMPLER_TABLE_MIRROR,
		SAMPLER_TABLE_CLAMP,
		
		NUM_SAMPLER_TABLE
	};

	struct ParticleInfo
	{
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT3 Velocity;
		float LifeTime;
	};

	bool createPipelineLayouts();
	bool createPipelines(XUSG::Format rtFormat);
	bool createDescriptorTables();

	void visualizeColor(const XUSG::CommandList& commandList);
	void rayCast(const XUSG::CommandList& commandList);
	void renderParticles(const XUSG::CommandList& commandList);
	void generateMatrices(DirectX::XMMATRIX& worldViewProj, DirectX::XMMATRIX* pWorldI = nullptr) const;

	XUSG::Device m_device;

	XUSG::ShaderPool				m_shaderPool;
	XUSG::Graphics::PipelineCache	m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache	m_computePipelineCache;
	XUSG::PipelineLayoutCache		m_pipelineLayoutCache;
	std::shared_ptr<XUSG::DescriptorTableCache> m_descriptorTableCache;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	XUSG::DescriptorTable	m_srvUavTables[NUM_SRV_UAV_TABLE];
	XUSG::DescriptorTable	m_samplerTables[NUM_SAMPLER_TABLE];

	XUSG::Texture3D			m_incompress;
	XUSG::Texture3D			m_velocities[2];
	XUSG::Texture3D			m_colors[2];
	XUSG::StructuredBuffer	m_particleBuffer;

	DirectX::XMUINT3		m_gridSize;
	DirectX::XMUINT2		m_viewport;
	DirectX::XMFLOAT4X4		m_viewProj;
	DirectX::XMFLOAT3		m_eyePt;

	float					m_timeStep;
	uint8_t					m_frameParity;
	uint32_t				m_numParticles;
};
