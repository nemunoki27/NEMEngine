#include "ShaderGraphGroupEditor.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCanvasID.h"
#include "ShaderGraphEditOperations.h"

// c++
#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
// imgui
#include <imgui.h>
#include <imgui_stdlib.h>
#include <imgui_node_editor.h>

using namespace Engine::ShaderGraphCanvasID;

namespace {

	namespace ed = ax::NodeEditor;
	constexpr float kGroupNameEditWidth = 200.0f;
	constexpr float kGroupHorizontalPadding = 40.0f;
	constexpr float kGroupTopPadding = 56.0f;
	constexpr float kGroupBottomPadding = 40.0f;
	constexpr float kDuplicateGroupSpacing = 48.0f;

}

Engine::ShaderGraphGroupEditor::ShaderGraphGroupEditor(
	ShaderGraphEditSession& session, const ShaderGraphAppearanceSetting& settings)
	: session_(session), settings_(settings) {
}

void Engine::ShaderGraphGroupEditor::DrawGroup(ShaderGraphGroup& group) {

	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * settings_.nodeTextScale);
	ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	const ed::NodeId groupID(ToNodeEditorID(group.id));
	const ImVec2 editorGroupSize = ed::GetNodeSize(groupID);
	const float groupWidth = 0.0f < editorGroupSize.x ? editorGroupSize.x : group.size.x;
	ed::BeginNode(groupID);
	ImGui::PushID(static_cast<int>(group.id.value));

	const ImVec2 headerMinimum = ImGui::GetCursorScreenPos();
	if (editingGroup_ == group.id) {
		const float editWidth = (std::min)(groupWidth, kGroupNameEditWidth);
		ImGui::SetCursorScreenPos(ImVec2(headerMinimum.x + (groupWidth - editWidth) * 0.5f, headerMinimum.y));
		ImGui::SetNextItemWidth(editWidth);
		if (requestGroupNameFocus_) {
			ImGui::SetKeyboardFocusHere();
			requestGroupNameFocus_ = false;
		}

		const std::string previousName = group.name;
		const bool committed = ImGui::InputText(
			"##GroupName", &group.name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		const bool deactivated = ImGui::IsItemDeactivated();
		if (group.name != previousName) {
			session_.MarkDirty();
		}
		if (committed || deactivated) {
			if (group.name.empty()) {
				group.name = "Group";
				session_.MarkDirty();
			}
			editingGroup_ = UUID{};
		}
	} else {
		const char* name = group.name.empty() ? "Group" : group.name.c_str();
		const float textWidth = ImGui::CalcTextSize(name).x;
		ImGui::SetCursorScreenPos(ImVec2(headerMinimum.x + (groupWidth - textWidth) * 0.5f, headerMinimum.y));
		ImGui::TextUnformatted(name);
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {

			editingGroup_ = group.id;
			requestGroupNameFocus_ = true;
		}
	}

	const float groupMinimumY = ImGui::GetCursorScreenPos().y;
	ImGui::SetCursorScreenPos(ImVec2(headerMinimum.x, groupMinimumY));
	ed::Group(ImVec2(groupWidth, group.size.y));
	ImGui::PopID();
	ed::EndNode();
	std::vector<ed::NodeId> memberIDs{};
	for (const ShaderGraphNode& node : session_.GetDraft().nodes) {
		if (node.groupID == group.id) {
			memberIDs.emplace_back(ToNodeEditorID(node.id));
		}
	}
	ed::SetGroupMembers(groupID, memberIDs.empty() ? nullptr : memberIDs.data(), static_cast<int>(memberIDs.size()));
	ed::PopStyleVar();

	if (ed::BeginGroupHint(groupID)) {

		const ImVec2 groupMinimum = ed::GetGroupMin();
		const ImVec2 groupMaximum = ed::GetGroupMax();
		const Vector2 groupSize{
			groupMaximum.x - groupMinimum.x,
			groupMaximum.y - groupMinimum.y,
		};
		if (group.size.x != groupSize.x || group.size.y != groupSize.y) {

			group.size = groupSize;
			session_.MarkDirty();
		}
	}
	ed::EndGroupHint();
	ImGui::PopFont();
}

void Engine::ShaderGraphGroupEditor::RemoveGroup(UUID groupID) {

	if (editingGroup_ == groupID) {
		editingGroup_ = UUID{};
		requestGroupNameFocus_ = false;
	}
	for (ShaderGraphNode& node : session_.GetDraft().nodes) {
		if (node.groupID == groupID) {
			node.groupID = UUID{};
		}
	}
	std::erase_if(session_.GetDraft().groups, [&](const ShaderGraphGroup& group) { return group.id == groupID; });
	session_.MarkDirty();
}

void Engine::ShaderGraphGroupEditor::DuplicateGroup(UUID groupID) {

	const auto found = std::find_if(session_.GetDraft().groups.begin(), session_.GetDraft().groups.end(),
		[&](const ShaderGraphGroup& group) { return group.id == groupID; });
	if (found == session_.GetDraft().groups.end()) {
		return;
	}

	const ShaderGraphGroup sourceGroup = *found;
	const ed::NodeId sourceGroupID(ToNodeEditorID(groupID));
	const ImVec2 sourcePosition = ed::GetNodePosition(sourceGroupID);
	const ImVec2 sourceSize = ed::GetNodeSize(sourceGroupID);
	const float sourceWidth = 0.0f < sourceSize.x ? sourceSize.x : sourceGroup.size.x;
	const ImVec2 duplicateOffset(sourceWidth + kDuplicateGroupSpacing, 0.0f);
	ShaderGraphGroup duplicateGroup = sourceGroup;
	duplicateGroup.id = UUID::New();
	duplicateGroup.name += " コピー";
	duplicateGroup.position = Vector2(sourcePosition.x + duplicateOffset.x, sourcePosition.y + duplicateOffset.y);
	duplicateGroup.size = Vector2(
		0.0f < sourceSize.x ? sourceSize.x : sourceGroup.size.x, 0.0f < sourceSize.y ? sourceSize.y : sourceGroup.size.y);
	const UUID newGroupID = duplicateGroup.id;

	// グループ内ノードと内部リンクだけを複製
	std::unordered_map<uint64_t, UUID> duplicateNodeIDs{};
	const size_t sourceNodeCount = session_.GetDraft().nodes.size();
	for (size_t index = 0; index < sourceNodeCount; ++index) {

		const ShaderGraphNode& source = session_.GetDraft().nodes[index];
		if (source.groupID != groupID || source.id == session_.GetDraft().outputNode ||
			source.id == session_.GetDraft().vertexOutputNode) {

			continue;
		}

		const ed::NodeId sourceNodeID(ToNodeEditorID(source.id));
		const ImVec2 nodePosition = ed::GetNodePosition(sourceNodeID);
		ShaderGraphNode duplicate = source;
		ShaderGraphEditOperations::RegenerateNodeIDs(duplicate);
		duplicate.groupID = newGroupID;
		duplicate.position = Vector2(nodePosition.x + duplicateOffset.x, nodePosition.y + duplicateOffset.y);
		duplicateNodeIDs.emplace(source.id.value, duplicate.id);
		const UUID duplicateID = duplicate.id;
		session_.GetDraft().nodes.emplace_back(std::move(duplicate));
		ed::SetNodePosition(ed::NodeId(ToNodeEditorID(duplicateID)),
			ImVec2(nodePosition.x + duplicateOffset.x, nodePosition.y + duplicateOffset.y));
	}

	const size_t sourceLinkCount = session_.GetDraft().links.size();
	for (size_t index = 0; index < sourceLinkCount; ++index) {

		const ShaderGraphLink& source = session_.GetDraft().links[index];
		const auto output = duplicateNodeIDs.find(source.outputNode.value);
		const auto input = duplicateNodeIDs.find(source.inputNode.value);
		if (output == duplicateNodeIDs.end() || input == duplicateNodeIDs.end()) {
			continue;
		}

		ShaderGraphLink duplicate = source;
		duplicate.id = UUID::New();
		duplicate.outputNode = output->second;
		duplicate.inputNode = input->second;
		session_.GetDraft().links.emplace_back(std::move(duplicate));
	}

	session_.GetDraft().groups.emplace_back(std::move(duplicateGroup));

	const ed::NodeId newEditorGroupID(ToNodeEditorID(newGroupID));
	ed::SetNodePosition(newEditorGroupID, ImVec2(sourcePosition.x + duplicateOffset.x, sourcePosition.y + duplicateOffset.y));
	ed::SetGroupSize(
		newEditorGroupID, ImVec2(session_.GetDraft().groups.back().size.x, session_.GetDraft().groups.back().size.y));
	ed::ClearSelection();
	ed::SelectNode(newEditorGroupID);
	session_.MarkDirty();
}

void Engine::ShaderGraphGroupEditor::GroupSelectedNodes(std::span<const UUID> selectedNodes) {

	if (selectedNodes.size() < 2) {
		return;
	}

	Vector2 minimum{
		(std::numeric_limits<float>::max)(),
		(std::numeric_limits<float>::max)(),
	};
	Vector2 maximum{
		(std::numeric_limits<float>::lowest)(),
		(std::numeric_limits<float>::lowest)(),
	};
	for (UUID nodeID : selectedNodes) {
		const ed::NodeId editorNodeID(ToNodeEditorID(nodeID));
		const ImVec2 position = ed::GetNodePosition(editorNodeID);
		const ImVec2 size = ed::GetNodeSize(editorNodeID);
		minimum.x = (std::min)(minimum.x, position.x);
		minimum.y = (std::min)(minimum.y, position.y);
		maximum.x = (std::max)(maximum.x, position.x + size.x);
		maximum.y = (std::max)(maximum.y, position.y + size.y);

		const auto node = std::find_if(session_.GetDraft().nodes.begin(), session_.GetDraft().nodes.end(),
			[&](const ShaderGraphNode& value) { return value.id == nodeID; });
		if (node != session_.GetDraft().nodes.end()) {
			node->position = Vector2(position.x, position.y);
		}
	}

	ShaderGraphGroup group{
		.id = UUID::New(),
		.name = "グループ " + std::to_string(session_.GetDraft().groups.size() + 1),
		.position = Vector2(minimum.x - kGroupHorizontalPadding, minimum.y - kGroupTopPadding),
		.size = Vector2(maximum.x - minimum.x + kGroupHorizontalPadding * 2.0f,
			maximum.y - minimum.y + kGroupTopPadding + kGroupBottomPadding),
	};
	const ed::NodeId groupID(ToNodeEditorID(group.id));
	for (UUID nodeID : selectedNodes) {
		const auto node = std::find_if(session_.GetDraft().nodes.begin(), session_.GetDraft().nodes.end(),
			[&](const ShaderGraphNode& value) { return value.id == nodeID; });
		if (node != session_.GetDraft().nodes.end()) {
			node->groupID = group.id;
		}
	}
	ed::SetNodePosition(groupID, ImVec2(group.position.x, group.position.y));
	ed::SetGroupSize(groupID, ImVec2(group.size.x, group.size.y));
	session_.GetDraft().groups.emplace_back(std::move(group));

	ed::ClearSelection();
	ed::SelectNode(groupID);
	session_.MarkDirty();
}

void Engine::ShaderGraphGroupEditor::Reset() {

	// 切替前の名前入力とfocus要求を解除する
	editingGroup_ = {};
	requestGroupNameFocus_ = false;
}
