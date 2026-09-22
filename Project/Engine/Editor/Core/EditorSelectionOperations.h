#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorContext.h"
#include "EditorState.h"
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

namespace Engine::EditorSelectionOperations {

	// 選択対象の複製を実行する
	bool Duplicate(const EditorContext* context, EditorState& state, IEditorPanelHost& host);
	// 選択対象のコピーを実行する
	bool Copy(const EditorContext& context, EditorState& state);
	// 選択対象の貼り付けを実行する
	bool Paste(const EditorContext* context, EditorState& state, IEditorPanelHost& host);
}
