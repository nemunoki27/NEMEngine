#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

namespace Engine::ShaderGraphEditOperations {

	// Graphの型に応じた既定値を作る
	MaterialParameterValue DefaultValueForGraphType(ShaderGraphValueType type);

	// Nodeとその接続を削除する
	void RemoveNode(ShaderGraphAsset& graph, UUID nodeID);
	// 複製したNodeとPortへ新しいIDを割り当てる
	void RegenerateNodeIDs(ShaderGraphNode& node);
} // Engine::ShaderGraphEditOperations
