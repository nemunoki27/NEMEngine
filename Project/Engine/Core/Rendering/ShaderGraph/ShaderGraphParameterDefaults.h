#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAsset.h"

namespace Engine::ShaderGraphParameterDefaults {

	// Materialへ公開する初期値を作る
	MaterialParameterSet Build(const ShaderGraphAsset& graph);
	// 未設定の値だけを初期値で補う
	void ApplyMissing(const MaterialParameterSet& defaults, MaterialParameterSet& parameters);
}
