#include "ShaderGraphPreviewPipeline.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactBuilder.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <format>

std::unique_ptr<Engine::ShaderGraphPreviewPipeline> Engine::ShaderGraphPreviewPipelineBuilder::Create(
	GraphicsCore& graphicsCore, const ShaderGraphNodePreviewCompileOutput& output, std::string& error) {

	error.clear();
	if (!output.Succeeded() || output.pixelHLSL.empty()) {
		for (const ShaderGraphDiagnostic& diagnostic : output.diagnostics) {
			if (diagnostic.severity == ShaderGraphDiagnosticSeverity::Error) {
				if (!error.empty()) {
					error += '\n';
				}
				error += diagnostic.message;
			}
		}
		if (error.empty()) {
			error = "プレビュー用Shaderを生成できませんでした";
		}
		return nullptr;
	}

	const uint64_t sourceHash = Algorithm::MixHashString(14695981039346656037ull, output.pixelHLSL);
	const std::filesystem::path path = RuntimePaths::GetLibraryPath(
		Algorithm::PathFromUTF8(std::format("ShaderGraph/NodePreviews/{:016x}.PS.hlsl", sourceHash)));
	if (!StorageFileUtility::WriteBytes(path, output.pixelHLSL)) {
		error = "プレビュー用Shaderを保存できませんでした: " + Algorithm::PathToUTF8(path);
		return nullptr;
	}

	GraphicsPipelineDesc desc{};
	desc.preRaster = {.file = "Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl", .entry = "main", .profile = "vs_6_0"};
	desc.pixel = {.file = Algorithm::PathToUTF8(path), .entry = "main", .profile = "ps_6_0"};
	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.StencilEnable = FALSE;
	desc.disableNormalBlending = true;
	desc.normalBlendOnly = true;
	desc.rtvFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

	PipelineStaticSamplerSettings defaultSampler{};
	defaultSampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	defaultSampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	defaultSampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	desc.staticSamplers.emplace_back(ShaderGraphArtifactBuilder::MakeGraphSampler(defaultSampler, 0));
	for (const ShaderGraphSamplerBinding& sampler : output.samplers) {
		desc.staticSamplers.emplace_back(ShaderGraphArtifactBuilder::MakeGraphSampler(sampler.settings, sampler.shaderRegister));
	}

	const ShaderAsset metadata = ShaderGraphArtifactBuilder::MakePixelShader(
		"ShaderGraphNodePreview", {}, path, "main", output.parameters);
	auto result = std::make_unique<ShaderGraphPreviewPipeline>();
	result->pipeline = PipelineStateBuilder::CreateGraphics(graphicsCore.GetDXObject().GetResourceRetirement(),
		graphicsCore.GetDXObject().GetDevice(), graphicsCore.GetDXObject().GetDxShaderCompiler(), desc, &metadata);
	if (!result->pipeline) {
		error = "プレビュー用Shaderのコンパイルに失敗しました: " + Algorithm::PathToUTF8(path);
		return nullptr;
	}
	result->layout.Build(result->pipeline->GetGraphicsReflection());
	return result;
}
