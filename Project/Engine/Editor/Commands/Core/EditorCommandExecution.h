#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/EditorCommandContext.h>

// c++
#include <functional>

namespace Engine {

	class EditorSceneDirtyState;

	//============================================================================
	//	EditorCommandExecution class
	//	編集操作の通知と失敗診断
	//============================================================================
	class EditorCommandExecution {
	public:
		// 操作単位でdirty通知と例外処理を行う
		static bool Run(const EditorContext& context, EditorState& state, EditorSceneDirtyState& dirtyState,
			const std::function<bool(EditorCommandContext&)>& operation);
	};
}
