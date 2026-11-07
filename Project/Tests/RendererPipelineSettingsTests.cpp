#include "RendererPipelineSettingsTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/RendererPipelineSettings.h>

bool NEMTests::TestRendererPipelineSettings() {

	using namespace Engine;
	RendererRenderSettings settings;
	settings.fillMode = RendererFillMode::WireFrame;
	settings.depthTest = false;
	settings.stencilEnabled = true;
	if (NormalizeRendererPipelineSettings(&settings) != NormalizeRendererPipelineSettings(nullptr)) return false;
	GraphicsPipelineDesc desc{};
	desc.rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	desc.rasterizer.CullMode = D3D12_CULL_MODE_FRONT;
	desc.depthStencil.DepthEnable = TRUE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	desc.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	ApplyRendererPipelineSettings(desc, settings);
	if (desc.rasterizer.FillMode != D3D12_FILL_MODE_SOLID || desc.rasterizer.CullMode != D3D12_CULL_MODE_FRONT ||
		!desc.depthStencil.DepthEnable || desc.depthStencil.DepthFunc != D3D12_COMPARISON_FUNC_LESS) return false;

	settings.inheritMaterial = false;
	settings.stencilEnabled = false;
	settings.stencilReference = 231;
	settings.stencilReadMask = 17;
	settings.stencilWriteMask = 93;
	settings.frontFace.pass = RendererStencilOperation::REPLACE;
	settings.separateBackFace = true;
	settings.backFace.fail = RendererStencilOperation::ZERO;
	auto normalized = NormalizeRendererPipelineSettings(&settings);
	ApplyRendererPipelineSettings(desc, normalized);
	if (desc.depthStencil.DepthEnable || desc.depthStencil.DepthWriteMask != D3D12_DEPTH_WRITE_MASK_ZERO ||
		desc.depthStencil.DepthFunc != D3D12_COMPARISON_FUNC_ALWAYS ||
		desc.rasterizer.FillMode != D3D12_FILL_MODE_WIREFRAME) return false;
	PipelineCacheKey first;
	first.rendererSettings = normalized;
	settings.stencilReference = 19;
	settings.stencilReadMask = 1;
	settings.backFace.fail = RendererStencilOperation::DECR;
	settings.depthComparison = RendererDepthComparison::GREATER;
	settings.depthWrite = false;
	PipelineCacheKey second;
	second.rendererSettings = NormalizeRendererPipelineSettings(&settings);
	if (!(first == second)) return false;

	settings.depthTest = true;
	settings.depthWrite = true;
	settings.stencilEnabled = true;
	settings.separateBackFace = false;
	settings.cullMode = RendererCullMode::NONE;
	settings.frontFace.comparison = RendererDepthComparison::EQUAL;
	settings.frontFace.pass = RendererStencilOperation::INCR;
	normalized = NormalizeRendererPipelineSettings(&settings);
	ApplyRendererPipelineSettings(desc, normalized);
	if (desc.rasterizer.CullMode != D3D12_CULL_MODE_NONE || !desc.depthStencil.DepthEnable ||
		desc.depthStencil.DepthWriteMask != D3D12_DEPTH_WRITE_MASK_ALL || !desc.depthStencil.StencilEnable ||
		desc.depthStencil.StencilReadMask != settings.stencilReadMask ||
		desc.depthStencil.StencilWriteMask != settings.stencilWriteMask ||
		desc.depthStencil.FrontFace.StencilFunc != D3D12_COMPARISON_FUNC_EQUAL ||
		desc.depthStencil.BackFace.StencilPassOp != D3D12_STENCIL_OP_INCR) return false;
	first.rendererSettings = normalized;
	settings.stencilReference = 255;
	settings.backFace.depthFail = RendererStencilOperation::INVERT;
	second.rendererSettings = NormalizeRendererPipelineSettings(&settings);
	if (!(first == second)) return false;
	settings.separateBackFace = true;
	second.rendererSettings = NormalizeRendererPipelineSettings(&settings);
	if (first == second) return false;
	ApplyRendererPipelineSettings(desc, second.rendererSettings);
	if (desc.depthStencil.BackFace.StencilDepthFailOp != D3D12_STENCIL_OP_INVERT ||
		desc.depthStencil.FrontFace.StencilDepthFailOp != D3D12_STENCIL_OP_KEEP) return false;

	for (uint8_t value = 1; value <= 8; ++value) {
		settings.depthComparison = static_cast<RendererDepthComparison>(value);
		settings.frontFace.fail = static_cast<RendererStencilOperation>(value);
		ApplyRendererPipelineSettings(desc, NormalizeRendererPipelineSettings(&settings));
		if (desc.depthStencil.DepthFunc != static_cast<D3D12_COMPARISON_FUNC>(value) ||
			desc.depthStencil.FrontFace.StencilFailOp != static_cast<D3D12_STENCIL_OP>(value)) return false;
	}
	return true;
}
