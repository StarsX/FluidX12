//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSG.h"
#include "Advanced/XUSGSphericalHarmonics.h"

class LightProbe
{
public:
	LightProbe();
	virtual ~LightProbe();

	bool Init(XUSG::CommandList* pCommandList, const XUSG::DescriptorTableCache::sptr& descriptorTableCache,
		std::vector<XUSG::Resource::uptr>& uploaders, const wchar_t* fileName,
		XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool CreateDescriptorTables(XUSG::Device* pDevice);

	void UpdateFrame(uint8_t frameIndex, DirectX::CXMMATRIX viewProj, const DirectX::XMFLOAT3& eyePt);
	void TransformSH(XUSG::CommandList* pCommandList);
	void RenderEnvironment(const XUSG::CommandList* pCommandList, uint8_t frameIndex);

	XUSG::ShaderResource* GetRadiance() const;
	XUSG::StructuredBuffer::sptr GetSH() const;

	static const uint8_t FrameCount = 3;
	static const uint8_t CubeMapFaceCount = 6;

protected:
	enum PipelineIndex : uint8_t
	{
		ENVIRONMENT,

		NUM_PIPELINE
	};

	bool createPipelineLayouts();
	bool createPipelines(XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool createDescriptorTables();

	XUSG::ShaderPool::sptr				m_shaderPool;
	XUSG::Graphics::PipelineCache::uptr	m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache::sptr	m_computePipelineCache;
	XUSG::PipelineLayoutCache::sptr		m_pipelineLayoutCache;
	XUSG::DescriptorTableCache::sptr	m_descriptorTableCache;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	XUSG::SphericalHarmonics::uptr m_sphericalHarmonics;

	XUSG::DescriptorTable	m_srvTable;

	XUSG::Texture::sptr	m_radiance;

	XUSG::ConstantBuffer::uptr m_cbPerFrame;
};
