#include "NodeGraphContext.h"

//============================================================================
//	NodeGraphContext classMethods
//============================================================================

Engine::NodeGraphContext::NodeGraphContext() {

	// 設定ファイルはEditor側で管理しないため、imgui-node-editorには保存させない
	ax::NodeEditor::Config config{};
	config.SettingsFile = nullptr;
	editor_ = ax::NodeEditor::CreateEditor(&config);
}

Engine::NodeGraphContext::~NodeGraphContext() {

	// ContextはCreateEditorした側で破棄する
	if (editor_) {
		ax::NodeEditor::DestroyEditor(editor_);
		editor_ = nullptr;
	}
}

bool Engine::NodeGraphContext::IsNodePlaced(GraphID nodeID) const {

	// 初回だけSetNodePositionするための判定
	return placedNodes_.contains(nodeID);
}

void Engine::NodeGraphContext::MarkNodePlaced(GraphID nodeID) {

	// imgui-node-editorへ初期座標を渡し終わったNodeを記録する
	placedNodes_.insert(nodeID);
}

void Engine::NodeGraphContext::ResetPlacedNodes() {

	// ImportやLayout Reset後は次の描画で全Nodeの座標を再設定する
	placedNodes_.clear();
}
