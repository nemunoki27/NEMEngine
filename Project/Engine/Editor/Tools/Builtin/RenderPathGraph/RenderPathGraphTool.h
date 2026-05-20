#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>
#include <Engine/Editor/Graph/GraphNodeRegistry.h>
#include <Engine/Editor/Graph/GraphValidationTypes.h>
#include <Engine/Editor/Graph/View/NodeGraphContext.h>
#include <Engine/Editor/Graph/View/NodeGraphView.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphCompiler.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphImporter.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphValidator.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
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

		// 取り込んだScene Path
		std::string importedScenePath_;
		// 下部に表示する状態メッセージ
		std::string statusMessage_;
		// Compile結果のPreview文字列
		std::string compilePreviewText_;

		// 編集中のGraph
		GraphDocument document_{};
		// RenderPath用Node定義
		GraphNodeRegistry registry_{};
		// 現在の検証結果
		GraphValidationResult validationResult_{};
		// Compile済みpassOrder
		std::vector<ScenePassDesc> compiledPassOrder_{};

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
		// NodeGraph本体を描画する
		void DrawNodeGraph(const EditorToolContext& context);
		// 選択NodeのPropertyを描画する
		void DrawNodeProperties(GraphNode& node);
		// 検証メッセージを描画する
		void DrawValidationMessages();
		// Compile結果を描画する
		void DrawCompilePreview();

		// 現在のSceneHeaderからGraphを取り込む
		void ImportFromCurrentScene(const EditorToolContext& context);
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
		// Node配置状態をリセットする
		void ResetLayout();

		// Scene変更前のBackupを作成する
		bool MakeSceneBackup(const EditorToolContext& context);
		// 現在Scene用のGraph保存Pathを作成する
		std::string MakeActiveGraphPath(const EditorToolContext& context) const;
	};
} // Engine
