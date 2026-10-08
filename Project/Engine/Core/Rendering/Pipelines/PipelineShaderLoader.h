#pragma once

//============================================================================
//	include
//============================================================================
#include "PipelineState.h"

namespace Engine::PipelineShaderLoader {

	struct GraphicsCompileResult {

		std::vector<CompiledShader> shaders;
		bool success = true;
	};

	// CookまたはSourceから指定stageを取得する
	bool CompileOne(std::vector<CompiledShader>& shaders, DxShaderCompiler* compiler,
		const ShaderCompileDesc& desc, ShaderStage stage, const char* stageName);
	// 描画stageを構成順に取得する
	GraphicsCompileResult Compile(DxShaderCompiler* compiler, const GraphicsPipelineDesc& desc);
	// 構築中Shaderの参照列を作る
	std::vector<const CompiledShader*> MakeShaderPointers(const std::vector<CompiledShader>& shaders);
}
