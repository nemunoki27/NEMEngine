#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCompiler.h"

namespace Engine {

	struct ShaderGraphNodePreviewOptions {

		bool sphere = false;
		bool rawOutput = false;
	};

	struct ShaderGraphNodePreviewCompileOutput {

		std::string pixelHLSL;
		ShaderGraphValueType valueType = ShaderGraphValueType::Invalid;
		std::vector<ShaderParameterMetadata> parameters;
		MaterialParameterSet defaultParameters;
		std::vector<ShaderGraphSamplerBinding> samplers;
		std::vector<ShaderGraphDiagnostic> diagnostics;
		bool usesTime = false;
		bool usesScene = false;
		bool usesGeometry = false;

		bool Succeeded() const;
	};

	// 値の更新とPipelineの作り直しを区別する識別情報を作る
	std::string BuildShaderGraphNodePreviewPipelineKey(const ShaderGraphNodePreviewCompileOutput& output);

	//============================================================================
	//	ShaderGraphNodePreviewCompiler class
	//	本番と同じ式から指定出力だけのプレビューを生成する
	//============================================================================
	class ShaderGraphNodePreviewCompiler {
	public:
		// 出力へつながるノードだけを展開して評価する
		static ShaderGraphNodePreviewCompileOutput Compile(const ShaderGraphAsset& graph, UUID nodeID,
			uint32_t outputSlot = 0, const ShaderGraphNodePreviewOptions& options = {},
			const ShaderGraphAssetResolver& resolver = {});
	};
} // Engine
