#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>

// c++
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

	//============================================================================
	//	ShaderGraphCompiler structures
	//============================================================================
	struct ShaderGraphDiagnostic {

		UUID node{};
		std::string message;
	};

	struct ShaderGraphCompileOutput {

		std::string surfaceHLSL;
		std::string opaquePixelHLSL;
		std::string transparentPixelHLSL;
		std::vector<ShaderParameterMetadata> parameters;
		std::vector<ShaderGraphDiagnostic> diagnostics;

		bool Succeeded() const { return diagnostics.empty(); }
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
			std::string_view surfaceIncludeFile);
	};
} // Engine
