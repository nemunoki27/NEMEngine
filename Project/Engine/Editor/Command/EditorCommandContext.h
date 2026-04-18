#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorContext.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>

// c++
#include <vector>

namespace Engine {

	// front
	struct EditorState;

	//============================================================================
	//	EditorCommandContext struct
	//	エディタコマンド実行時に必要な情報
	//============================================================================
	struct EditorCommandContext {

		// 現在のエディタ参照情報
		const EditorContext* editorContext = nullptr;
		// エディタ状態
		EditorState* editorState = nullptr;

		// ワールド全体のランタイム階層リンクを再構築する
		void RebuildHierarchyAll() const;

		// 編集可能か
		bool CanEditScene() const { return editorContext && !editorContext->isPlaying && editorContext->activeWorld; }
		// ワールド取得
		ECSWorld* GetWorld() const { return editorContext ? editorContext->activeWorld : nullptr; }
	};
} // Engine