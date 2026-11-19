#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphNodePreviewCompiler.h"
#include "ShaderGraphExpressionCompiler.h"

namespace Engine::ShaderGraphNodePreviewSource {

	// 共通の入力と表示変換を付けてPixelShaderを作る
	std::string Build(const ShaderGraphAsset& graph, ShaderGraphExpressionCompiler& compiler,
		const ShaderGraphExpression& value, const ShaderGraphNodePreviewOptions& options);
} // Engine::ShaderGraphNodePreviewSource
