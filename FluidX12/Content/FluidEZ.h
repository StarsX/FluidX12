//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Helper/XUSG-EZ.h"

class FluidEZ
{
public:
	enum RenderFlags : uint8_t
	{
		RAY_MARCH_DIRECT = 0,
		RAY_MARCH_CUBEMAP = (1 << 0),
		SEPARATE_LIGHT_PASS = (1 << 1),
		OPTIMIZED = RAY_MARCH_CUBEMAP | SEPARATE_LIGHT_PASS
	};

	FluidEZ();
	virtual ~FluidEZ();

	bool Init(XUSG::CommandList* pCommandList, uint32_t width, uint32_t height,
		std::vector<XUSG::Resource::uptr>& uploaders, XUSG::Format rtFormat, XUSG::Format dsFormat,
		const DirectX::XMUINT3& gridSize);

	void SetMaxSamples(uint32_t maxRaySamples, uint32_t maxLightSamples);
	void SetSH(const XUSG::StructuredBuffer::sptr& coeffSH);
	void UpdateFrame(float timeStep, uint8_t frameIndex, const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& eyePt);
	void Simulate(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex);
	void Render(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex, uint8_t flags);

	static const uint8_t FrameCount = 3;

protected:
	enum ShadeIndex : uint8_t
	{
		CS_ADVECT,
		CS_PROJECT_3D,
		CS_PROJECT_2D,
		CS_RAY_MARCH,
		CS_RAY_MARCH_L,
		CS_RAY_MARCH_V,
		VS_CUBE,
		PS_CUBE,
		VS_SCREEN_QUAD,
		PS_RAY_CAST,
		PS_RAY_CAST_V,
		PS_VISUALIZE_COLOR,
		
		NUM_SHADER
	};

	enum CBSampleResType
	{
		CB_SAMPLE_RES,
		CB_SAMPLE_RES_L,

		NUM_CB_SAMPLE_RES
	};

	bool createShaders();

	void visualizeColor(XUSG::EZ::CommandList* pCommandList);
	void rayMarch(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex);
	void rayMarchL(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex);
	void rayMarchV(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex);
	void renderCube(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex);
	void rayCastDirect(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex);
	void rayCastVDirect(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex);

	XUSG::ShaderLib::uptr		m_shaderLib;
	XUSG::Blob					m_shaders[NUM_SHADER];

	XUSG::Texture3D::uptr	m_incompress;
	XUSG::Texture3D::uptr	m_velocities[2];
	XUSG::Texture3D::uptr	m_colors[2];
	XUSG::Texture2D::uptr	m_cubeMap;
	XUSG::Texture3D::uptr	m_lightMap;

	XUSG::ConstantBuffer::uptr m_cbSimulation;
	XUSG::ConstantBuffer::uptr m_cbPerObject;
	XUSG::ConstantBuffer::uptr m_cbPerFrame;
	XUSG::ConstantBuffer::uptr m_cbSampleRes[NUM_CB_SAMPLE_RES];
#if _CPU_CUBE_FACE_CULL_ == 1
	XUSG::ConstantBuffer::uptr	m_cbCubeFaceCull;
#elif _CPU_CUBE_FACE_CULL_ == 2
	XUSG::ConstantBuffer::uptr	m_cbCubeFaceList;
#endif

	XUSG::StructuredBuffer::sptr m_coeffSH;
	XUSG::Buffer::uptr		m_nullBuffer;

	DirectX::XMFLOAT3		m_lightPt;
	DirectX::XMFLOAT4		m_lightColor;
	DirectX::XMFLOAT4		m_ambient;
	DirectX::XMUINT3		m_gridSize;
	DirectX::XMUINT3		m_lightMapSize;
	DirectX::XMUINT2		m_viewport;
	DirectX::XMFLOAT3X4		m_volumeWorld;

	uint32_t				m_raySampleCount;
	uint32_t				m_maxRaySamples;
	uint32_t				m_maxLightSamples;
	uint8_t					m_cubeFaceCount;
	uint8_t					m_cubeMapLOD;
	uint8_t					m_frameParity;

	float					m_timeStep;
	float					m_timeInterval;
};

