#include "ShaderGraphCanvasContext.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCanvasID.h"

// c++
#include <algorithm>
// imgui
#include <imgui.h>
#include <imgui_node_editor.h>

using namespace Engine::ShaderGraphCanvasID;

namespace {

	namespace ed = ax::NodeEditor;
}

Engine::ShaderGraphCanvasContext::~ShaderGraphCanvasContext() {

	Reset();
}

void Engine::ShaderGraphCanvasContext::EnsureCreated() {

	if (nodeEditor_) {
		return;
	}
	// Graphの保存位置を使うCanvasを作成する
	ed::Config config{};
	config.SettingsFile = nullptr;
	nodeEditor_ = ed::CreateEditor(&config);
	restoreNodePositions_ = true;
}

void Engine::ShaderGraphCanvasContext::Reset() {

	// 所有するCanvasを破棄する
	if (nodeEditor_) {
		// 破棄したCanvasを現在の編集対象へ残さない
		if (ed::GetCurrentEditor() == nodeEditor_) {
			ed::SetCurrentEditor(nullptr);
		}
		ed::DestroyEditor(nodeEditor_);
		nodeEditor_ = nullptr;
	}
	restoreNodePositions_ = true;
}

void Engine::ShaderGraphCanvasContext::CapturePositions(ShaderGraphAsset& graph) {

	if (!nodeEditor_) {
		return;
	}
	// 保存する位置をCanvasから取得する
	ed::SetCurrentEditor(nodeEditor_);
	for (ShaderGraphNode& node : graph.nodes) {
		const ImVec2 position = ed::GetNodePosition(ed::NodeId(ToNodeEditorID(node.id)));
		node.position = Vector2(position.x, position.y);
	}
	for (ShaderGraphGroup& group : graph.groups) {
		const ed::NodeId groupID(ToNodeEditorID(group.id));
		const ImVec2 position = ed::GetNodePosition(groupID);
		group.position = Vector2(position.x, position.y);
	}
	ed::SetCurrentEditor(nullptr);
}

void Engine::ShaderGraphCanvasContext::RestorePositions(const ShaderGraphAsset& graph) {

	if (!restoreNodePositions_) {
		return;
	}
	// Graphの位置とGroupサイズを復元する

	for (const ShaderGraphNode& node : graph.nodes) {
		ed::SetNodePosition(ed::NodeId(ToNodeEditorID(node.id)), ImVec2(node.position.x, node.position.y));
	}
	for (const ShaderGraphGroup& group : graph.groups) {
		const ed::NodeId groupID(ToNodeEditorID(group.id));
		ed::SetNodePosition(groupID, ImVec2(group.position.x, group.position.y));
		ed::SetGroupSize(groupID, ImVec2(group.size.x, group.size.y));
	}
	restoreNodePositions_ = false;
}

void Engine::ShaderGraphCanvasContext::NavigateToNode(UUID nodeID) {

	if (!nodeEditor_ || !nodeID) {
		return;
	}
	// 診断対象のNodeを選択して表示位置を合わせる
	ed::SetCurrentEditor(nodeEditor_);
	ed::ClearSelection();
	ed::SelectNode(ed::NodeId(ToNodeEditorID(nodeID)));
	ed::NavigateToSelection(true);
	ed::SetCurrentEditor(nullptr);
}

std::vector<Engine::UUID> Engine::ShaderGraphCanvasContext::GetSelectedNodes(const ShaderGraphAsset& graph) const {

	const int32_t selectedCount = ed::GetSelectedNodes(nullptr, 0);
	if (selectedCount <= 0) {
		return {};
	}

	std::vector<ed::NodeId> selectedIDs(static_cast<size_t>(selectedCount));
	const int32_t writtenCount = ed::GetSelectedNodes(selectedIDs.data(), selectedCount);

	std::vector<UUID> nodes;
	nodes.reserve(static_cast<size_t>(writtenCount));
	for (int32_t index = 0; index < writtenCount; ++index) {

		const UUID nodeID{static_cast<uint64_t>(selectedIDs[index].Get())};
		const auto found = std::find_if(
			graph.nodes.begin(), graph.nodes.end(), [&](const ShaderGraphNode& node) { return node.id == nodeID; });
		if (found != graph.nodes.end()) {
			nodes.emplace_back(nodeID);
		}
	}
	return nodes;
}
