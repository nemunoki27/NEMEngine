#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"

namespace Engine {

	//============================================================================
	//	ShaderGraphGenericNodeInspector class
	//	汎用ノードの演算方式と初期値を編集する
	//============================================================================
	class ShaderGraphGenericNodeInspector {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphGenericNodeInspector() = delete;
		~ShaderGraphGenericNodeInspector() = delete;

		// ノード固有値を編集する
		static void Draw(ShaderGraphEditSession& session, ShaderGraphNode& node);
	};
}
