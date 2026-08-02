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

	using ShaderGraphAssetResolver =
		std::function<bool(AssetID, ShaderGraphAsset&)>;

	//============================================================================
	//	ShaderGraphCompiler structures
	//============================================================================
	struct ShaderGraphSamplerBinding {

		UUID node{};
		std::string shaderName;
		uint32_t shaderRegister = 0;
		PipelineStaticSamplerSettings settings{};
	};

	struct ShaderGraphCompileOutput {

		std::string surfaceHLSL;
		std::string opaquePixelHLSL;
		std::string transparentPixelHLSL;
		std::string depthPixelHLSL;
		std::string pickingPixelHLSL;
		std::string vertexHLSL;
		std::string meshHLSL;
		std::string computeHLSL;
		std::vector<ShaderParameterMetadata> parameters;
		std::vector<ShaderGraphSamplerBinding> samplers;
		std::vector<ShaderGraphDiagnostic> diagnostics;
		ShaderGraphIRModule ir;

		bool Succeeded() const;
	};

	//============================================================================
	//	ShaderGraphCompiler class
	//	グラフを描画API非依存のSurface関数とRaster用PSへ変換する
	//============================================================================
	class ShaderGraphCompiler {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ShaderGraphCompiler() = delete;
		~ShaderGraphCompiler() = delete;

		static ShaderGraphCompileOutput Compile(
			const ShaderGraphAsset& graph,
			std::string_view surfaceIncludeFile,
			const ShaderGraphAssetResolver& resolver = {});
	};
} // Engine
