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
	// Inspectorに表示する公開値を宣言順で集める
	std::vector<MaterialParameterID> CollectExposed(const ShaderGraphAsset& graph);
	// 旧形式の共有値と元グラフの差から上書きを復元する
	void ImportLegacyOverrides(const ShaderGraphAsset& graph, MaterialAsset& material);
	// 同じIDと型を持つ編集済みの公開値を引き継ぐ
	void RestoreOverrides(const ShaderGraphAsset& graph, const MaterialAsset& previous, MaterialAsset& material);
	// 未設定の値だけを初期値で補う
	void ApplyMissing(const MaterialParameterSet& defaults, MaterialParameterSet& parameters);
	// 公開値の編集を保ち内部値は最新のGraphへ揃える
	void ApplyToMaterial(const MaterialParameterSet& defaults, std::span<const ShaderParameterMetadata> metadata,
		MaterialParameterSet& parameters);
}
