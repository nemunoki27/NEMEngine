#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphArtifactCache.h"

namespace Engine::ShaderGraphMaterialBuilder {

	// Graphの公開値と描画設定からMaterialを作る
	MaterialAsset CreateMaterial(const ShaderGraphAsset& graph, AssetID graphID);
	// 生成成果物をMaterialの各Passへ割り当てる
	void ApplyToMaterial(const ShaderGraphArtifact& artifact, MaterialAsset& material);
}
