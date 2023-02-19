//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Helper/XUSG-EZ.h"

class LightProbeEZ
{
public:
	LightProbeEZ();
	virtual ~LightProbeEZ();

	bool Init(XUSG::CommandList* pCommandList, std::vector<XUSG::Resource::uptr>& uploaders, const wchar_t* fileName);

	void UpdateFrame(uint8_t frameIndex, DirectX::CXMMATRIX viewProj, const DirectX::XMFLOAT3& eyePt);
	void TransformSH(XUSG::EZ::CommandList* pCommandList);
	void RenderEnvironment(XUSG::EZ::CommandList* pCommandList, uint8_t frameIndex);

	XUSG::Texture::sptr GetRadiance() const;
	XUSG::StructuredBuffer::sptr GetSH() const;

	static const uint8_t FrameCount = 3;
	static const uint8_t CubeMapFaceCount = 6;

protected:
	enum ShaderIndex : uint8_t
	{
		VS_SCREEN_QUAD,
		PS_ENVIRONMENT,

		CS_SH_CUBE_MAP,
		CS_SH_SUM,
		CS_SH_NORMALIZE,

		NUM_SHADER
	};

	bool createShaders();

	void shCubeMap(XUSG::EZ::CommandList* pCommandList, uint8_t order);
	void shSum(XUSG::EZ::CommandList* pCommandList, uint8_t order);
	void shNormalize(XUSG::EZ::CommandList* pCommandList, uint8_t order);

	XUSG::ShaderLib::sptr m_shaderLib;
	XUSG::Blob m_shaders[NUM_SHADER];

	XUSG::Texture::sptr m_radiance;

	XUSG::StructuredBuffer::sptr m_coeffSH[2];
	XUSG::StructuredBuffer::uptr m_weightSH[2];

	XUSG::ConstantBuffer::uptr m_cbPerFrame;
	XUSG::ConstantBuffer::uptr m_cbCubeMapSlices;
	XUSG::ConstantBuffer::uptr m_cbSHCubeMap;
	XUSG::ConstantBuffer::uptr m_cbSHSums;

	uint32_t	m_numSHTexels;
	uint8_t		m_shBufferParity;
};
