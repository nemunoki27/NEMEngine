#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAsset.h"

// c++
#include <span>

namespace Engine::ShaderGraphParameterDefaults {

	// Materialへ公開する初期値を作る
	MaterialParameterSet Build(const ShaderGraphAsset& graph);
	// 未設定の値だけを初期値で補う
	void ApplyMissing(const MaterialParameterSet& defaults, MaterialParameterSet& parameters);
	// 公開値の編集を保ち内部値は最新のGraphへ揃える
	void ApplyToMaterial(const MaterialParameterSet& defaults, std::span<const ShaderParameterMetadata> metadata,
		MaterialParameterSet& parameters);
}
