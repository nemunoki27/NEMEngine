#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCompiler.h"

namespace Engine::ShaderGraphSubGraphExpander {

	// SubGraphを展開し参照の循環を検証する
	bool Expand(
		ShaderGraphAsset& graph, const ShaderGraphAssetResolver& resolver, std::vector<ShaderGraphDiagnostic>& diagnostics);
} // Engine::ShaderGraphSubGraphExpander
