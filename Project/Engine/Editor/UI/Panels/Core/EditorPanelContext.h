#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Core/EditorLayoutState.h>

namespace Engine {

	// front
	class IEditorPanelHost;
	class ViewportRenderService;
	class GraphicsCore;
	class GraphicsPlatform;
	class RenderPipelineRunner;
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
		// RenderTextureなど、GPUリソース作成に必要なグラフィックス全体の管理
		GraphicsCore* graphicsCore = nullptr;
		// グラフィックス機能の状態を管理
		GraphicsPlatform* graphicsPlatform = nullptr;
		// エディタツールのプレビュー描画で使用する描画パイプライン
		RenderPipelineRunner* renderPipeline = nullptr;
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
