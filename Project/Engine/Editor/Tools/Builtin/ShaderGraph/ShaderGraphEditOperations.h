#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

namespace Engine::ShaderGraphEditOperations {

	// Graphの型に応じた既定値を作る
	MaterialParameterValue DefaultValueForGraphType(ShaderGraphValueType type);
	// 保存済みのVector4とColorから四成分を読み取る
	Vector4 GetVector4Value(const MaterialParameterValue& value, const Vector4& fallback = {});
	// 保存済みのVector4とColorを色として読み取る
	Color4 GetColorValue(const MaterialParameterValue& value);

	// Nodeとその接続を削除する
	void RemoveNode(ShaderGraphAsset& graph, UUID nodeID);
	// 複製したNodeとPortへ新しいIDを割り当てる
	void RegenerateNodeIDs(ShaderGraphNode& node);
} // Engine::ShaderGraphEditOperations
