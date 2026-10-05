#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "ShaderGraphAppearanceEditor.h"
#include "ShaderGraphScenePreview.h"
#include "ShaderGraphPreviewController.h"
#include "ShaderGraphSettingsEditor.h"
#include "ShaderGraphNodePreviews.h"
#include "ShaderGraphNodeDrawer.h"
#include "ShaderGraphCanvasContext.h"
#include "ShaderGraphNodeTransfer.h"
#include "ShaderGraphGroupEditor.h"
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include "ShaderGraphEditSession.h"
#include "ShaderGraphAssetAuthoring.h"
#include "ShaderGraphToolbar.h"
#include "ShaderGraphParameterEditor.h"
#include "ShaderGraphKeywordEditor.h"
#include "ShaderGraphNodeInspector.h"
#include "ShaderGraphCanvasMenu.h"
#include "ShaderGraphCanvasView.h"

// c++
#include <memory>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	ShaderGraphEditorTool class
	//	Graphの編集画面と保存とpreviewを接続する
	//============================================================================
	class ShaderGraphEditorTool :
		public IEditorTool, private IShaderGraphToolbarActions {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ShaderGraphEditorTool();
		~ShaderGraphEditorTool() override;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		bool HasPendingEdits() const override;
		void RequestResolvePendingEdits() override;
		EditorToolCloseResult ConsumePendingEditCloseResult() override;
		// ProjectPanelから指定グラフを開く
		void OpenAsset(AssetID assetID);

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.shader_graph",
			.name = "シェーダーグラフ",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 2,
		};

		bool openWindow_ = false;
		AssetID pendingAsset_{};
		AssetID requestedAsset_{};
		bool requestAssetSwitch_ = false;
		bool requestGraphCreate_ = false;
		bool requestWindowClose_ = false;
		bool requestUnsavedPrompt_ = false;
		EditorToolCloseResult pendingEditCloseResult_ =
			EditorToolCloseResult::None;
		ShaderGraphEditSession editSession_;
		bool commandPanelFocused_ = false;
		ShaderGraphParameterEditor parameterEditor_;
		ShaderGraphKeywordEditor keywordEditor_;
		ShaderGraphNodeInspector nodeInspector_;
		ShaderGraphToolbar toolbar_;
		ShaderGraphCanvasContext canvas_;
		ShaderGraphAppearanceEditor appearanceEditor_;
		ShaderGraphNodePreviews nodePreviews_;
		ShaderGraphNodeTransfer nodeTransfer_;
		ShaderGraphNodeDrawer nodeDrawer_;
		ShaderGraphGroupEditor groupEditor_;
		ShaderGraphCanvasMenu canvasMenu_;
		ShaderGraphCanvasView canvasView_;
		// シーン上のMaterialプレビュー
		ShaderGraphScenePreview scenePreview_;
		ShaderGraphPreviewController previewController_;
		ShaderGraphSettingsEditor settingsEditor_;

		//--------- functions ----------------------------------------------------

		// ツールの編集画面を表示する
		void DrawWindow(const EditorToolContext& context);
		// 未保存の編集を切替前に解決する
		void DrawUnsavedPrompt(const EditorToolContext& context);
		// 公開Parameterの一覧を表示する
		void DrawParameterPanel(const EditorToolContext& context);
		// Node Editorの外観を編集する
		void DrawAppearancePanel();
		// GraphのNodeと接続を表示する
		void DrawGraph(const EditorToolContext& context);
		// Graphの描画設定を編集する
		void DrawGraphSettings(const EditorToolContext& context);
		// 選択Nodeの詳細を編集する
		void DrawSelectedNodeEditor(const EditorToolContext& context);

		// Graphを読み編集画面を切り替える
		bool LoadGraph(const EditorToolContext& context, AssetID assetID);
		// 未保存状態を確認してグラフ切替を予約する
		void RequestGraphSwitch(const EditorToolContext& context, AssetID assetID) override;
		// 確認済みの切替または終了を適用する
		void ApplyPendingTransition(const EditorToolContext& context);
		// 別アセットの設定を検証して一括置換する
		void ImportGraphSettings(const EditorToolContext& context, AssetID source) override;
		// 未保存状態を確認してGraph作成を要求する
		void RequestGraphCreation(const EditorToolContext& context) override;
		// 表示位置を確定してGraphを保存する
		bool SaveGraph(const EditorToolContext& context) override;
		// 初期Graphを作成して開く
		bool CreateGraph(const EditorToolContext& context);
		// 位置を確定してGraphの保存とコンパイルを要求する
		bool SaveAndCompile(const EditorToolContext& context) override;
		// Sceneの元のMaterialを復元する
		void RestorePreviewMaterial(const EditorToolContext& context);
		// Node Editorの位置をGraphへ取り込む
		void CaptureNodePositions();
		// Node Editorの表示状態を再作成する
		void ResetNodeEditor();
		// 選択Nodeをクリップボードへコピーする
		void CopySelection();
		// クリップボードのNodeを配置する
		void PasteSelection();
		// Graphの編集を元に戻す
		void UndoGraph() override;
		// Graphの編集をやり直す
		void RedoGraph() override;
		// 編集内容をGraphの履歴へ確定する
		void CommitGraphHistory();
		// 選択しているNodeを取得する
		std::vector<UUID> GetSelectedGraphNodes() const;
	};
} // Engine
