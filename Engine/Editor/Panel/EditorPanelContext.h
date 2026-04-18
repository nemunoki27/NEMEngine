#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorContext.h>
#include <Engine/Editor/EditorState.h>
#include <Engine/Editor/EditorLayoutState.h>

namespace Engine {

	// front
	class IEditorPanelHost;
	class ViewportRenderService;
	class GraphicsPlatform;
	struct ResolvedRenderView;

	//============================================================================
	//	EditorPanelContext struct
	//============================================================================

	// 各パネルに共通で渡す描画コンテキスト
	struct EditorPanelContext {

		// エディタ全体のコンテキスト
		const EditorContext* editorContext = nullptr;
		// エディタ全体の状態
		EditorState* editorState = nullptr;
		// エディタのレイアウト状態
		EditorLayoutState* layoutState = nullptr;
		// パネルが操作を依頼するためのホスト
		IEditorPanelHost* host = nullptr;
		// グラフィックス機能の状態を管理
		GraphicsPlatform* graphicsPlatform = nullptr;
		// ビューポート描画に必要なリソースを管理するサービス
		const ViewportRenderService* viewportRenderService = nullptr;
		const ResolvedRenderView* sceneRenderView = nullptr;

		ECSWorld* GetWorld() const { return editorContext ? editorContext->activeWorld : nullptr; }

		// プレイモードかどうか
		bool IsPlaying() const { return editorContext ? editorContext->isPlaying : false; }
		// シーンを編集できる状態かどうか
		bool CanEditScene() const { return editorContext && !editorContext->isPlaying && editorContext->activeWorld; }
	};
} // Engine