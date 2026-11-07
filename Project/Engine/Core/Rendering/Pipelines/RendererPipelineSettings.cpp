#include "RendererPipelineSettings.h"

//============================================================================
//	RendererPipelineSettings functions
//============================================================================
Engine::RendererRenderSettings Engine::NormalizeRendererPipelineSettings(const RendererRenderSettings* settings) {

	if (!settings || settings->inheritMaterial) return {};
	RendererRenderSettings result = *settings;
	result.stencilReference = 0;
	if (!result.depthTest) {
		result.depthWrite = false;
		result.depthComparison = RendererDepthComparison::ALWAYS;
	}
	if (!result.stencilEnabled) {
		result.stencilReadMask = 255;
		result.stencilWriteMask = 255;
		result.frontFace = {};
		result.backFace = {};
		result.separateBackFace = false;
	} else if (!result.separateBackFace) {
		result.backFace = result.frontFace;
	}
	return result;
}

void Engine::ApplyRendererPipelineSettings(GraphicsPipelineDesc& desc, const RendererRenderSettings& settings) {

	if (settings.inheritMaterial) return;
	desc.rasterizer.FillMode = static_cast<D3D12_FILL_MODE>(settings.fillMode);
	desc.rasterizer.CullMode = static_cast<D3D12_CULL_MODE>(settings.cullMode);
	desc.depthStencil.DepthEnable = settings.depthTest;
	desc.depthStencil.DepthWriteMask = settings.depthTest && settings.depthWrite ?
		D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(settings.depthComparison);
	desc.depthStencil.StencilEnable = settings.stencilEnabled;
	desc.depthStencil.StencilReadMask = settings.stencilReadMask;
	desc.depthStencil.StencilWriteMask = settings.stencilWriteMask;
	const auto face = [](const RendererStencilFace& source) {
		return D3D12_DEPTH_STENCILOP_DESC{
			static_cast<D3D12_STENCIL_OP>(source.fail),
			static_cast<D3D12_STENCIL_OP>(source.depthFail),
			static_cast<D3D12_STENCIL_OP>(source.pass),
			static_cast<D3D12_COMPARISON_FUNC>(source.comparison) };
	};
	desc.depthStencil.FrontFace = face(settings.frontFace);
	desc.depthStencil.BackFace = face(settings.separateBackFace ? settings.backFace : settings.frontFace);
}
