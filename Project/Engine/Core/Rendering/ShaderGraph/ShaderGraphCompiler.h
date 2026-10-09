#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphIR.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>

// c++
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

	using ShaderGraphAssetResolver = std::function<bool(AssetID, ShaderGraphAsset&)>;

	//============================================================================
	//	ShaderGraphCompiler structures
	//============================================================================
	// NodeのSampler設定とShader上の配置
	struct ShaderGraphSamplerBinding {

		UUID node{};
		std::string shaderName;
		uint32_t shaderRegister = 0;
		PipelineStaticSamplerSettings settings{};
	};

	// 描画経路ごとの生成Shaderと型情報
	struct ShaderGraphCompileOutput {

		std::string surfaceHLSL;
		std::string opaquePixelHLSL;
		std::string transparentPixelHLSL;
		std::string depthPixelHLSL;
		std::string pickingPixelHLSL;
		std::string outlinePixelHLSL;
		std::string vertexHLSL;
		std::string meshHLSL;
		std::string rayTracingHLSL;
		std::string giMaterialHLSL;
		std::string giVertexHLSL;
		std::string computeHLSL;
		std::vector<ShaderParameterMetadata> parameters;
		MaterialParameterSet defaultParameters;
		std::vector<ShaderGraphSamplerBinding> samplers;
		std::vector<ShaderGraphDiagnostic> diagnostics;
		ShaderGraphIRModule ir;

		// エラー診断がないことを確認する
		bool Succeeded() const;
	};

	//============================================================================
	//	ShaderGraphCompiler class
	//	Graphを描画経路ごとのShaderへ変換する
	//============================================================================
	class ShaderGraphCompiler {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ShaderGraphCompiler() = delete;
		~ShaderGraphCompiler() = delete;

		// Graphを検証して描画経路ごとのShaderを生成する
		static ShaderGraphCompileOutput Compile(const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile,
			const ShaderGraphAssetResolver& resolver = {});
	};
}
