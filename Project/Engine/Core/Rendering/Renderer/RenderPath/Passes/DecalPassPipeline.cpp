#include "DecalPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>

void Engine::DecalPass::EnsurePipeline(GraphicsCore& graphicsCore) {

	if (pipeline_) return;
	GraphicsPipelineDesc desc;
	desc.preRaster = {.file = "Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl", .entry = "main", .profile = "vs_6_0",
		.shader = BuiltinAssets::Shaders::FullscreenCopy};
	desc.pixel = {.file = "Builtin/Decal/decal.PS.hlsl", .entry = "main", .profile = "ps_6_0",
		.shader = BuiltinAssets::Shaders::Decal};
	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.StencilEnable = FALSE;
	desc.numRenderTargets = 4;
	desc.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.rtvFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.rtvFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.rtvFormats[3] = DXGI_FORMAT_R11G11B10_FLOAT;
	// 色と法線はAlpha合成し発光だけを加算する
	D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	blend.IndependentBlendEnable = TRUE;
	for (uint32_t index = 0; index < 4; ++index) {
		auto& target = blend.RenderTarget[index];
		target.BlendEnable = TRUE;
		target.SrcBlend = index == 3 ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA;
		target.DestBlend = index == 3 ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
		target.BlendOp = D3D12_BLEND_OP_ADD;
		target.SrcBlendAlpha = D3D12_BLEND_ZERO;
		target.DestBlendAlpha = D3D12_BLEND_ONE;
		target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN;
		if (index != 2) target.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_BLUE;
	}
	desc.blendOverride = blend;
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler.MaxAnisotropy = 1;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 1;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	desc.staticSamplers.push_back(sampler);
	pipeline_ = PipelineStateBuilder::CreateGraphics(graphicsCore.GetDXObject().GetResourceRetirement(),
		graphicsCore.GetDXObject().GetDevice(), graphicsCore.GetDXObject().GetDxShaderCompiler(), desc);
}
