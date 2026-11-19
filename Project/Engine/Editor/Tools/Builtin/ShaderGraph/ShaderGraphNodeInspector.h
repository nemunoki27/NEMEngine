#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"

// c++
#include <span>

namespace Engine {

	struct EditorToolContext;

	//============================================================================
	//	ShaderGraphNodeInspector class
	//	選択Nodeの設定を編集する
	//============================================================================
	class ShaderGraphNodeInspector {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 選択Nodeの型と値と接続設定を表示する
		void Draw(const EditorToolContext& context, ShaderGraphEditSession& session, std::span<const UUID> selected);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// プレビューの形状と出力を編集する
		void DrawPreviewSettings(ShaderGraphEditSession& session, ShaderGraphNode& node);
	};
} // Engine
