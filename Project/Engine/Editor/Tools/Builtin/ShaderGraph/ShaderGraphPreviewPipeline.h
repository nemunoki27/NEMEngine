#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodePreviewCompiler.h>

// c++
#include <memory>
#include <string>

namespace Engine {

	class GraphicsCore;

	struct ShaderGraphPreviewPipeline {

		std::unique_ptr<PipelineState> pipeline;
		MaterialParameterLayout layout;
	};

	//============================================================================
	//	ShaderGraphPreviewPipelineBuilder class
	//	生成ソースと型情報からノードプレビューの描画構成を作る
	//============================================================================
	class ShaderGraphPreviewPipelineBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphPreviewPipelineBuilder() = delete;
		~ShaderGraphPreviewPipelineBuilder() = delete;

		// プレビュー用Shaderを保存してPipelineと定数配置を生成する
		static std::unique_ptr<ShaderGraphPreviewPipeline> Create(GraphicsCore& graphicsCore,
			const ShaderGraphNodePreviewCompileOutput& output, std::string& error);
	};
} // Engine
