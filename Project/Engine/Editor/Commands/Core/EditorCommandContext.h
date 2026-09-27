#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

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
		// Play中のInspectorから実行Worldだけを編集する
		bool allowRuntimeEdit = false;

		// ワールド全体のランタイム階層リンクを再構築する
		void RebuildHierarchyAll() const;

		// 編集対象Worldが有効か
		bool CanEditScene() const {
			return editorContext && editorContext->activeWorld &&
				(!editorContext->isPlaying || allowRuntimeEdit);
		}
		// ワールド取得
		ECSWorld* GetWorld() const { return editorContext ? editorContext->activeWorld : nullptr; }
	};
} // Engine
