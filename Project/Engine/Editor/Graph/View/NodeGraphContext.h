#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphID.h>

// c++
#include <unordered_set>
// imgui-node-editor
#include <imgui_node_editor.h>

namespace Engine {

	//============================================================================
	//	NodeGraphContext class
	//	imgui-node-editorのContextと配置状態を管理するクラス
	//============================================================================
	class NodeGraphContext {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// コンストラクタ
		NodeGraphContext();
		// デストラクタ
		~NodeGraphContext();
		NodeGraphContext(const NodeGraphContext&) = delete;
		NodeGraphContext& operator=(const NodeGraphContext&) = delete;

		//--------- accessor -----------------------------------------------------

		// Nodeの初回配置が済んでいるか
		bool IsNodePlaced(GraphID nodeID) const;
		// Nodeを配置済みとして記録する
		void MarkNodePlaced(GraphID nodeID);
		// 配置済みNodeの記録をリセットする
		void ResetPlacedNodes();

		// imgui-node-editorのContextを取得する
		ax::NodeEditor::EditorContext* Get() const { return editor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// imgui-node-editorのContext
		ax::NodeEditor::EditorContext* editor_ = nullptr;
		// 初回配置済みのNode ID
		std::unordered_set<GraphID> placedNodes_;
	};
} // Engine
