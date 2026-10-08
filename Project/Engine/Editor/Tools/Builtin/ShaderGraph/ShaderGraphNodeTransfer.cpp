#include "ShaderGraphNodeTransfer.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCanvasID.h"
#include "ShaderGraphEditOperations.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
// imgui
#include <imgui.h>
#include <imgui_node_editor.h>

using namespace Engine::ShaderGraphCanvasID;

namespace {

	namespace ed = ax::NodeEditor;
	constexpr float kDuplicateOffset = 32.0f;
}

Engine::ShaderGraphNodeTransfer::ShaderGraphNodeTransfer(ShaderGraphEditSession& session) : session_(session) {
}

void Engine::ShaderGraphNodeTransfer::DuplicateNode(UUID nodeID) {

	const auto found = std::find_if(session_.GetDraft().nodes.begin(), session_.GetDraft().nodes.end(),
		[&](const ShaderGraphNode& node) { return node.id == nodeID; });
	if (found == session_.GetDraft().nodes.end() || nodeID == session_.GetDraft().outputNode ||
		nodeID == session_.GetDraft().vertexOutputNode) {
		return;
	}

	// NodeとPortのIDを更新して複製する
	const ImVec2 sourcePosition = ed::GetNodePosition(ed::NodeId(ToNodeEditorID(nodeID)));
	ShaderGraphNode duplicate = *found;
	ShaderGraphEditOperations::RegenerateNodeIDs(duplicate);
	duplicate.position = Vector2(sourcePosition.x + kDuplicateOffset, sourcePosition.y + kDuplicateOffset);
	const UUID duplicateID = duplicate.id;
	session_.GetDraft().nodes.emplace_back(std::move(duplicate));
	ed::SetNodePosition(ed::NodeId(ToNodeEditorID(duplicateID)),
		ImVec2(sourcePosition.x + kDuplicateOffset, sourcePosition.y + kDuplicateOffset));

	ed::ClearSelection();
	ed::SelectNode(ed::NodeId(ToNodeEditorID(duplicateID)));
	session_.MarkDirty();
}

void Engine::ShaderGraphNodeTransfer::CopySelection(std::span<const UUID> selected) {

	if (selected.empty()) {
		return;
	}
	std::unordered_set<uint64_t> selectedIDs;
	for (UUID id : selected) {
		selectedIDs.insert(id.value);
	}
	// 選択範囲内のNodeと接続を保存する
	ShaderGraphAsset fragment{};
	fragment.name = "Clipboard";
	fragment.target = session_.GetDraft().target;
	for (const ShaderGraphNode& node : session_.GetDraft().nodes) {
		if (!selectedIDs.contains(node.id.value) || node.id == session_.GetDraft().outputNode ||
			node.id == session_.GetDraft().vertexOutputNode) {
			continue;
		}
		fragment.nodes.emplace_back(node);
	}
	if (fragment.nodes.empty()) {
		return;
	}
	fragment.outputNode = fragment.nodes.front().id;
	for (const ShaderGraphLink& link : session_.GetDraft().links) {
		if (selectedIDs.contains(link.outputNode.value) && selectedIDs.contains(link.inputNode.value)) {
			fragment.links.emplace_back(link);
		}
	}
	const std::string text = "NEM_SHADER_GRAPH_CLIPBOARD\n" + ToJson(fragment).dump();
	ImGui::SetClipboardText(text.c_str());
	session_.GetStatusMessage() = "選択ノードをコピーしました";
}

bool Engine::ShaderGraphNodeTransfer::PasteSelection() {

	// Graph形式のクリップボードを検証する
	const char* clipboard = ImGui::GetClipboardText();
	if (!clipboard) {
		return false;
	}
	constexpr std::string_view kHeader = "NEM_SHADER_GRAPH_CLIPBOARD\n";
	const std::string_view text(clipboard);
	if (!text.starts_with(kHeader)) {
		return false;
	}

	const nlohmann::json data = nlohmann::json::parse(text.substr(kHeader.size()), nullptr, false);
	ShaderGraphAsset fragment{};
	if (data.is_discarded() || !FromJson(data, fragment)) {
		session_.GetStatusMessage() = "コピーしたノードを読み込めませんでした";
		return false;
	}

	std::unordered_map<uint64_t, UUID> idMap;
	ed::ClearSelection();
	for (ShaderGraphNode& node : fragment.nodes) {
		const UUID sourceID = node.id;
		ShaderGraphEditOperations::RegenerateNodeIDs(node);
		node.groupID = UUID{};
		idMap[sourceID.value] = node.id;
		node.position.x += 32.0f;
		node.position.y += 32.0f;
		const UUID nodeID = node.id;
		const Vector2 position = node.position;
		session_.GetDraft().nodes.emplace_back(std::move(node));
		ed::SetNodePosition(ed::NodeId(ToNodeEditorID(nodeID)), ImVec2(position.x, position.y));
		ed::SelectNode(ed::NodeId(ToNodeEditorID(nodeID)), true);
	}
	for (ShaderGraphLink& link : fragment.links) {
		const auto source = idMap.find(link.outputNode.value);
		const auto destination = idMap.find(link.inputNode.value);
		if (source == idMap.end() || destination == idMap.end()) {
			continue;
		}
		link.id = UUID::New();
		link.outputNode = source->second;
		link.inputNode = destination->second;
		session_.GetDraft().links.emplace_back(std::move(link));
	}
	session_.MarkDirty();
	session_.GetStatusMessage() = "ノードを貼り付けました";
	return true;
}
