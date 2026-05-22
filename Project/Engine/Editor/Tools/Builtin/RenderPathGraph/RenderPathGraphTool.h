#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>
#include <Engine/Editor/Graph/GraphNodeRegistry.h>
#include <Engine/Editor/Graph/GraphSerializer.h>
#include <Engine/Editor/Graph/GraphValidationTypes.h>
#include <Engine/Editor/Graph/View/NodeGraphContext.h>
#include <Engine/Editor/Graph/View/NodeGraphView.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphCompiler.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphImporter.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphValidator.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	RenderPathGraphTool class
	//	SceneHeader.passOrderをNode Graphとして編集するEditor Tool
	//============================================================================

	class RenderPathGraphTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// コンストラクタ
		RenderPathGraphTool();
		// デストラクタ
		~RenderPathGraphTool() override = default;

		// Toolを開く
		void OpenEditorTool() override;
		// Tool本体を描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		// Tool情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Tool登録情報
		ToolDescriptor descriptor_{
			.id = "engine.render_path_graph",
			.name = "RenderPath Graph",
			.category = "Rendering",
			.description = "Scene passOrder graph editor",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly | ToolFlags::Experimental,
			.order = 20,
		};

		// Windowを開いているか
		bool openWindow_ = false;
		// 現在のSceneからGraphを取り込み済みか
		bool imported_ = false;
		// Graphに未適用の変更があるか
		bool graphDirty_ = false;
		// 検証結果を更新する必要があるか
		bool validationDirty_ = true;
		// Graph保存ファイルへ反映する必要があるか
		bool graphSaveDirty_ = false;

		// 取り込んだScene Path
		std::string importedScenePath_;
		// 下部に表示する状態メッセージ
		std::string statusMessage_;
		// Compile結果のPreview文字列
		std::string compilePreviewText_;
		// Compile結果の差分文字列
		std::string compileDiffText_;
		// Resource使用範囲のPreview文字列
		std::string resourceLifetimeText_;
		// Barrier確認用のPreview文字列
		std::string barrierPreviewText_;
		// Node検索文字列
		char filterText_[128]{};
		// 検証メッセージがあるNodeを強調する
		bool filterHasValidationMessage_ = false;
		// Materialを使うNodeを強調する
		bool filterUsesMaterial_ = false;
		// Depth入力を使うNodeを強調する
		bool filterReadsDepth_ = false;
		// Viewへ出すNodeを強調する
		bool filterWritesView_ = false;
		// 次描画でGraph全体を表示するか
		bool requestFitToGraph_ = false;
		// Minimap overlayを表示するか
		bool showMinimap_ = false;
		// Graph Undo用の事前スナップショット
		nlohmann::json preDrawSnapshot_{};
		// 事前スナップショットが有効か
		bool preDrawSnapshotValid_ = false;
		// マウスUp時にUndoスナップショットを確定する必要があるか
		bool undoPending_ = false;
		// Undo用スナップショットスタック
		std::vector<nlohmann::json> undoStack_{};
		// Redo用スナップショットスタック
		std::vector<nlohmann::json> redoStack_{};

		// コンパイル結果から構築したパスキャプチャ情報
		std::vector<RenderPathFrameCapturePass> capturedPasses_{};

		// 編集中のGraph
		GraphDocument document_{};
		// RenderPath用Node定義
		GraphNodeRegistry registry_{};
		// 現在の検証結果
		GraphValidationResult validationResult_{};
		// Compile済み描画情報
		RenderPathGraphCompileResult compiledResult_{};

		// NodeGraph UI Context
		NodeGraphContext graphContext_{};
		// NodeGraph描画クラス
		NodeGraphView graphView_{};
		// SceneHeaderからGraphを作成するImporter
		RenderPathGraphImporter importer_{};
		// Graph検証クラス
		RenderPathGraphValidator validator_{};
		// GraphからpassOrderを作成するCompiler
		RenderPathGraphCompiler compiler_{};

		//--------- functions ----------------------------------------------------

		// Window全体を描画する
		void DrawWindow(const EditorToolContext& context);
		// 上部Toolbarを描画する
		void DrawToolbar(const EditorToolContext& context);
		// 右側Panelを描画する
		void DrawSidePanel(const EditorToolContext& context);
		// Node Style編集Windowを描画する
		void DrawNodeStyleEditWindow();
		// NodeGraph本体を描画する
		void DrawNodeGraph(const EditorToolContext& context);
		// 選択NodeのPropertyを描画する
		void DrawNodeProperties(GraphNode& node, const EditorToolContext& context);
		// Resource一覧を描画する
		void DrawResourceBlackboard(const EditorToolContext& context);
		// Template追加UIを描画する
		void DrawTemplatePanel(const EditorToolContext& context);
		// Node検索UIを描画する
		void DrawFilterPanel();
		// 検証メッセージを描画する
		void DrawValidationMessages();
		// Compile結果を描画する
		void DrawCompilePreview();
		// Resource解析結果を描画する
		void DrawResourceAnalysis();

		// 現在のSceneHeaderからGraphを取り込む
		void ImportFromCurrentScene(const EditorToolContext& context, bool preferSavedGraph = false);
		// 現在のGraphを検証する
		void ValidateCurrentGraph(const EditorToolContext& context);
		// GraphをpassOrderへCompileしてPreviewを更新する
		void CompilePreview(const EditorToolContext& context);
		// Compile結果をSceneへ適用する
		void ApplyToScene(const EditorToolContext& context);
		// 現在のSceneを保存する
		void SaveScene(const EditorToolContext& context);
		// Graphファイルへ書き出す
		void ExportGraph(const EditorToolContext& context);
		// Graphファイルを読み込む
		void ImportGraph(const EditorToolContext& context);
		// Graphファイルへ自動保存する
		void AutoSaveGraph(const EditorToolContext& context);
		// Node配置状態をリセットする
		void ResetLayout();
		// Graphをリセットしてテンプレートを適用する
		void ApplyTemplate(const std::string& templateName);
		// 既存Graphへテンプレートを追加する
		void AppendTemplate(const std::string& templateName);
		// Compile結果からResource解析文字列を更新する
		void UpdateResourceAnalysisText();
		// Undo用スナップショットを保存する
		void PushUndoSnapshot(nlohmann::json snapshot);
		// Graph操作をUndoする
		void UndoGraph();
		// Graph操作をRedoする
		void RedoGraph();
		// Minimap overlayをCanvas上に描画する
		void DrawMinimap();

		// Scene変更前のBackupを作成する
		bool MakeSceneBackup(const EditorToolContext& context);
		// 現在Scene用のGraph保存Pathを作成する
		std::string MakeActiveGraphPath(const EditorToolContext& context) const;
		// Node Styleの保存Pathを作成する
		std::filesystem::path MakeNodeStyleConfigPath() const;
		// Node Styleを読み込む
		void LoadNodeStyleConfig();
		// Node Styleを保存する
		void SaveNodeStyleConfig();
	};
} // Engine
