#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"

namespace Engine {

	//============================================================================
	//	ShaderGraphNodeCreation class
	//	Nodeの初期値と参照を検証して編集Graphへ追加する
	//============================================================================
	class ShaderGraphNodeCreation {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphNodeCreation(ShaderGraphEditSession& session);

		// 種類に応じた既定Nodeを追加する
		UUID AddNode(ShaderGraphNodeKind kind, Vector2 position);
		// 指定型の定数Nodeを追加する
		UUID AddConstantNode(ShaderGraphValueType type, Vector2 position);
		// 存在するParameterの参照Nodeを追加する
		UUID AddParameterNode(UUID parameterID, Vector2 position);
		// 存在するKeywordの参照Nodeを追加する
		UUID AddKeywordNode(UUID keywordID, Vector2 position);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 操作対象の編集session
		ShaderGraphEditSession& session_;

		//--------- functions ----------------------------------------------------

		// Nodeを公開して編集状態を更新する
		UUID Append(ShaderGraphNode node);
	};
} // Engine
