//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen & ZENG, Wei. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSG.h"

class Fluid
{
public:
	enum RenderFlags : uint8_t
	{
		RAY_MARCH_DIRECT = 0,
		RAY_MARCH_CUBEMAP = (1 << 0),
		SEPARATE_LIGHT_PASS = (1 << 1),
		OPTIMIZED = RAY_MARCH_CUBEMAP | SEPARATE_LIGHT_PASS
	};

	Fluid();
	virtual ~Fluid();

	bool Init(XUSG::CommandList* pCommandList, uint32_t width, uint32_t height,
		const XUSG::DescriptorTableCache::sptr& descriptorTableCache,
		std::vector<XUSG::Resource::uptr>& uploaders, XUSG::Format rtFormat, XUSG::Format dsFormat,
		const DirectX::XMUINT3& gridSize);

	void SetMaxSamples(uint32_t maxRaySamples, uint32_t maxLightSamples);
	void SetSH(const XUSG::StructuredBuffer::sptr& coeffSH);
	void UpdateFrame(float timeStep, uint8_t frameIndex, const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& eyePt);
	void Simulate(XUSG::CommandList* pCommandList, uint8_t frameIndex);
	void Render(XUSG::CommandList* pCommandList, uint8_t frameIndex, uint8_t flags);

	static const uint8_t FrameCount = 3;

protected:
	enum PipelineIndex : uint8_t
	{
		ADVECT,
		PROJECT,
		RAY_MARCH,
		RAY_MARCH_L,
		RAY_MARCH_V,
		RENDER_CUBE,
		DIRECT_RAY_CAST,
		DIRECT_RAY_CAST_V,

		NUM_PIPELINE,
		VISUALIZE = RAY_MARCH
	};

	enum SrvUavTable : uint8_t
	{
		SRV_UAV_TABLE_VECOLITY,
		SRV_UAV_TABLE_VECOLITY1,
		SRV_UAV_TABLE_COLOR,
		SRV_UAV_TABLE_COLOR1,
		SRV_TABLE_RAY_MARCH,
		SRV_TABLE_RAY_MARCH1,
		UAV_TABLE_INCOMPRESS,
		UAV_TABLE_LIGHT_MAP,

		NUM_SRV_UAV_TABLE
	};

	bool createPipelineLayouts();
	bool createPipelines(XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool createDescriptorTables();

	void visualizeColor(const XUSG::CommandList* pCommandList);
	void rayMarch(XUSG::CommandList* pCommandList, uint8_t frameIndex);
	void rayMarchL(const XUSG::CommandList* pCommandList, uint8_t frameIndex);
	void rayMarchV(XUSG::CommandList* pCommandList, uint8_t frameIndex);
	void renderCube(XUSG::CommandList* pCommandList, uint8_t frameIndex);
	void rayCastDirect(const XUSG::CommandList* pCommandList, uint8_t frameIndex);
	void rayCastVDirect(XUSG::CommandList* pCommandList, uint8_t frameIndex);

	XUSG::ShaderPool::uptr				m_shaderPool;
	XUSG::Graphics::PipelineCache::uptr	m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache::uptr	m_computePipelineCache;
	XUSG::PipelineLayoutCache::uptr		m_pipelineLayoutCache;
	XUSG::DescriptorTableCache::sptr	m_descriptorTableCache;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	std::vector<XUSG::DescriptorTable> m_uavMipTables;
	std::vector<XUSG::DescriptorTable> m_srvMipTables;
	XUSG::DescriptorTable	m_srvUavTables[NUM_SRV_UAV_TABLE];
	XUSG::DescriptorTable	m_cbvTables[FrameCount];

	XUSG::Texture3D::uptr	m_incompress;
	XUSG::Texture3D::uptr	m_velocities[2];
	XUSG::Texture3D::uptr	m_colors[2];
	XUSG::Texture2D::uptr	m_cubeMap;
	XUSG::Texture3D::uptr	m_lightMap;

	XUSG::ConstantBuffer::uptr m_cbSimulation;
	XUSG::ConstantBuffer::uptr m_cbPerObject;
	XUSG::ConstantBuffer::uptr m_cbPerFrame;
#if _CPU_CUBE_FACE_CULL_ == 2
	XUSG::ConstantBuffer::uptr	m_cbCubeFaceList;
#endif

	XUSG::StructuredBuffer::sptr m_coeffSH;

	DirectX::XMFLOAT3		m_lightPt;
	DirectX::XMFLOAT4		m_lightColor;
	DirectX::XMFLOAT4		m_ambient;
	DirectX::XMUINT3		m_gridSize;
	DirectX::XMUINT3		m_lightMapSize;
	DirectX::XMUINT2		m_viewport;
	DirectX::XMFLOAT3X4		m_volumeWorld;
	DirectX::XMFLOAT3X4		m_lightMapWorld;

	uint32_t				m_raySampleCount;
	uint32_t				m_maxRaySamples;
	uint32_t				m_maxLightSamples;
#if _CPU_CUBE_FACE_CULL_ == 1
	uint32_t				m_visibilityMask;
#endif
	uint8_t					m_cubeFaceCount;
	uint8_t					m_cubeMapLOD;
	uint8_t					m_frameParity;

	float					m_timeStep;
	float					m_timeInterval;
};
