#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>
#include <Engine/Editor/Graph/GraphNodeRegistry.h>
#include <Engine/Editor/Graph/View/NodeGraphContext.h>
#include <Engine/Editor/Graph/View/NodeGraphStyle.h>

// c++
#include <functional>

namespace Engine {

	//============================================================================
	//	NodeGraphViewDesc structure
	//	NodeGraphView描画時に渡す設定
	//============================================================================
	struct NodeGraphViewDesc {

		//--------- variables ----------------------------------------------------

		// imgui-node-editorへ渡すEditor ID
		const char* editorID = "NodeGraph";
		// Node生成に使用するRegistry
		GraphNodeRegistry* registry = nullptr;
		// NodeのプロパティUI描画
		std::function<void(GraphNode&)> drawNodeProperty;
		// Node内のドラッグアンドドロップ受付
		std::function<void(GraphNode&)> drawNodeDropTarget;
		// 背景メニューからNode追加を要求した時のコールバック
		std::function<void(const std::string&, const ImVec2&)> addNodeRequested;
		// Nodeを強調表示するか
		std::function<bool(const GraphNode&)> isNodeHighlighted;
		// グループフレームNodeか(trueを返すNodeはed::Groupで描画する)
		std::function<bool(const GraphNode&)> isGroupNode;
		// 描画後にGraph全体へFitする
		bool navigateToContent = false;
	};

	//============================================================================
	//	NodeGraphView class
	//	imgui-node-editorを使用してGraphDocumentを描画するクラス
	//============================================================================
	class NodeGraphView {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// GraphEditor本体を描画する
		bool Draw(NodeGraphContext& context, GraphDocument& document, const NodeGraphViewDesc& desc);

		// 描画Styleを取得する
		const NodeGraphStyle& GetStyle() const { return style_; }
		// 描画Styleを取得する
		NodeGraphStyle& GetStyle() { return style_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// NodeGraphの描画Style
		NodeGraphStyle style_{};
		// Node追加メニューの検索文字列
		char nodeSearchText_[128]{};

		//--------- functions ----------------------------------------------------

		// Nodeを描画する
		void DrawNode(GraphDocument& document, GraphNode& node, const NodeGraphViewDesc& desc);
		// グループフレームNodeを描画する
		void DrawGroupNode(GraphNode& node, const NodeGraphViewDesc& desc);
		// Linkを描画する
		void DrawLinks(const GraphDocument& document);
		// Link作成操作を処理する
		bool DrawCreateLink(GraphDocument& document);
		// Node / Link削除操作を処理する
		bool DrawDelete(GraphDocument& document);
		// 背景右クリックメニューを描画する
		bool DrawBackgroundMenu(const NodeGraphViewDesc& desc);
	};
} // Engine

