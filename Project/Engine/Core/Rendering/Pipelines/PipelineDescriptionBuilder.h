#pragma once

//============================================================================
//	include
//============================================================================
#include "PipelineState.h"
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>

#include <span>

namespace Engine::PipelineDescriptionBuilder {

	// Assetと描画先からGraphics構成を確定する
	bool BuildGraphicsPipelineDesc(const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset,
		std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat,
		const PipelineStaticSamplerOverrideSet* samplerOverrides, GraphicsPipelineDesc& outDesc);
	// AssetからCompute構成を確定する
	bool BuildComputePipelineDesc(const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset,
		const PipelineStaticSamplerOverrideSet* samplerOverrides, ComputePipelineDesc& outDesc);
}
