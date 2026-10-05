#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"

namespace Engine {

	struct EditorToolContext;

	//============================================================================
	//	ShaderGraphParameterEditor class
	//	公開Parameterの選択と値編集を保持する
	//============================================================================
	class ShaderGraphParameterEditor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Parameterの一覧と設定を表示する
		void Draw(const EditorToolContext& context, ShaderGraphEditSession& session);
		// Graph切替時に選択を解除する
		void ResetSelection();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		int32_t selected_ = -1;

		//--------- functions ----------------------------------------------------

		// 指定Parameterの値を編集する
		void DrawValue(const EditorToolContext& context, ShaderGraphEditSession& session, ShaderGraphParameter& parameter);
		// Parameterと参照Nodeを削除する
		void Remove(ShaderGraphEditSession& session, uint32_t index);
	};
} // Engine
