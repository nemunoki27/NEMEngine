#include "ShaderGraphCanvasMenu.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCanvasContext.h"
#include "ShaderGraphCanvasID.h"
#include "ShaderGraphEditOperations.h"
#include "ShaderGraphGroupEditor.h"
#include "ShaderGraphNodeTransfer.h"
#include "ShaderGraphNodePreviewUtility.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

// c++
#include <algorithm>
#include <array>
// imgui
#include <imgui.h>
#include <imgui_stdlib.h>
#include <imgui_node_editor.h>

using namespace Engine::ShaderGraphCanvasID;
using namespace Engine::ShaderGraphNodePreviewUtility;

namespace {

	namespace ed = ax::NodeEditor;
	constexpr const char* kCreateNodePopup = "ShaderGraphCreateNode";
	constexpr const char* kNodeContextPopup = "ShaderGraphNodeContext";
}

Engine::ShaderGraphCanvasMenu::ShaderGraphCanvasMenu(ShaderGraphEditSession& session, ShaderGraphCanvasContext& canvas,
	ShaderGraphGroupEditor& groupEditor, ShaderGraphNodeTransfer& nodeTransfer)
	: editSession_(session), canvas_(canvas), groupEditor_(groupEditor), nodeTransfer_(nodeTransfer), nodeCreation_(session) {
}

void Engine::ShaderGraphCanvasMenu::OpenNode(UUID nodeID) {

	contextNode_ = nodeID;
	ImGui::OpenPopup(kNodeContextPopup);
}

void Engine::ShaderGraphCanvasMenu::OpenCreate(Vector2 position) {

	createNodePosition_ = position;
	ImGui::OpenPopup(kCreateNodePopup);
}

void Engine::ShaderGraphCanvasMenu::Reset() {

	contextNode_ = {};
}

void Engine::ShaderGraphCanvasMenu::Draw() {

	if (ImGui::BeginPopup(kNodeContextPopup)) {
		const auto group = std::find_if(editSession_.GetDraft().groups.begin(), editSession_.GetDraft().groups.end(),
			[&](const ShaderGraphGroup& value) { return value.id == contextNode_; });
		const auto node = std::find_if(editSession_.GetDraft().nodes.begin(), editSession_.GetDraft().nodes.end(),
			[&](const ShaderGraphNode& value) { return value.id == contextNode_; });

		if (group != editSession_.GetDraft().groups.end()) {
			ImGui::TextUnformatted(group->name.c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("グループを複製")) {
				groupEditor_.DuplicateGroup(contextNode_);
			}
			if (ImGui::MenuItem("グループを削除")) {
				groupEditor_.RemoveGroup(contextNode_);
			}
		} else {
			if (node != editSession_.GetDraft().nodes.end()) {
				ImGui::TextUnformatted(GetShaderGraphNodeName(node->kind).data());
			}
			ImGui::Separator();
			if (node != editSession_.GetDraft().nodes.end() && IsPreviewableNode(node->kind)) {

				if (ImGui::MenuItem("プレビューを表示", nullptr, node->previewExpanded)) {

					node->previewExpanded = !node->previewExpanded;
					editSession_.MarkDirty();
				}
				ImGui::Separator();
			}
			ImGui::BeginDisabled(
				node == editSession_.GetDraft().nodes.end() || contextNode_ == editSession_.GetDraft().outputNode);
			if (ImGui::MenuItem("ノードを複製")) {
				DuplicateNode(contextNode_);
			}
			if (ImGui::MenuItem("ノードを削除")) {
				editSession_.RemoveNode(contextNode_);
			}
			ImGui::EndDisabled();

			const bool canGroup = 2 <= canvas_.GetSelectedNodes(editSession_.GetDraft()).size();
			ImGui::BeginDisabled(!canGroup);
			if (ImGui::MenuItem("選択ノードをグループ化")) {

				groupEditor_.GroupSelectedNodes(canvas_.GetSelectedNodes(editSession_.GetDraft()));
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}

	DrawNodeCreationMenu();
}

void Engine::ShaderGraphCanvasMenu::DrawNodeCreationMenu() {

	if (!ImGui::BeginPopup(kCreateNodePopup)) {
		return;
	}

	const bool canGroup = 2 <= canvas_.GetSelectedNodes(editSession_.GetDraft()).size();
	ImGui::BeginDisabled(!canGroup);
	if (ImGui::MenuItem("選択ノードをグループ化")) {

		groupEditor_.GroupSelectedNodes(canvas_.GetSelectedNodes(editSession_.GetDraft()));
	}
	ImGui::EndDisabled();
	if (ImGui::BeginMenu("プレビュー")) {
		if (ImGui::MenuItem("すべて展開")) {
			for (ShaderGraphNode& node : editSession_.GetDraft().nodes) {

				if (IsPreviewableNode(node.kind)) {
					node.previewExpanded = true;
				}
			}
			editSession_.MarkDirty();
		}
		if (ImGui::MenuItem("すべて折りたたむ")) {
			for (ShaderGraphNode& node : editSession_.GetDraft().nodes) {

				if (IsPreviewableNode(node.kind)) {
					node.previewExpanded = false;
				}
			}
			editSession_.MarkDirty();
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	ImGui::SetNextItemWidth(280.0f);
	ImGui::InputTextWithHint("##NodeSearch", "ノードを検索", &nodeSearch_);
	if (editSession_.GetDraft().domain == ShaderGraphDomain::Surface &&
		SupportsShaderGraphVertexOutput(editSession_.GetDraft().target) && !editSession_.GetDraft().vertexOutputNode &&
		ImGui::MenuItem("頂点出力を追加")) {

		AddNode(ShaderGraphNodeKind::VertexOutput, createNodePosition_);
	}
	if (ImGui::BeginMenu("定数")) {
		const std::array constantTypes{
			ShaderGraphValueType::Float,
			ShaderGraphValueType::Float2,
			ShaderGraphValueType::Float3,
			ShaderGraphValueType::Float4,
			ShaderGraphValueType::Color,
			ShaderGraphValueType::Boolean,
			ShaderGraphValueType::Integer,
		};
		for (ShaderGraphValueType type : constantTypes) {
			if (ImGui::MenuItem(EnumAdapter<ShaderGraphValueType>::ToString(type))) {
				AddConstantNode(type, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}

	const std::string search = Algorithm::ToLower(nodeSearch_);
	const auto isCreatable = [this](const ShaderGraphNodeDescriptor& descriptor) {
		if (descriptor.kind == ShaderGraphNodeKind::RayTrace) {
			return editSession_.GetDraft().domain == ShaderGraphDomain::RayTracingEffect;
		}
		return descriptor.kind != ShaderGraphNodeKind::SurfaceOutput && descriptor.kind != ShaderGraphNodeKind::UnlitOutput &&
			   descriptor.kind != ShaderGraphNodeKind::PostProcessOutput &&
			   descriptor.kind != ShaderGraphNodeKind::RayTracingOutput &&
			   descriptor.kind != ShaderGraphNodeKind::VertexOutput && descriptor.kind != ShaderGraphNodeKind::Parameter &&
			   descriptor.kind != ShaderGraphNodeKind::Constant && descriptor.kind != ShaderGraphNodeKind::Keyword;
	};
	const auto& descriptors = ShaderGraphNodeRegistry::GetDescriptors();
	if (!search.empty()) {
		for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
			if (!isCreatable(descriptor)) {
				continue;
			}
			const std::string searchable =
				Algorithm::ToLower(std::string(descriptor.name) + " " + std::string(descriptor.category));
			if (searchable.find(search) != std::string::npos && ImGui::MenuItem(descriptor.name.data())) {
				AddNode(descriptor.kind, createNodePosition_);
			}
		}
	} else {
		std::vector<std::string_view> categories;
		for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
			if (isCreatable(descriptor) &&
				std::find(categories.begin(), categories.end(), descriptor.category) == categories.end()) {
				categories.emplace_back(descriptor.category);
			}
		}
		for (std::string_view category : categories) {
			if (!ImGui::BeginMenu(category.data())) {
				continue;
			}
			for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
				if (isCreatable(descriptor) && descriptor.category == category && ImGui::MenuItem(descriptor.name.data())) {
					AddNode(descriptor.kind, createNodePosition_);
				}
			}
			ImGui::EndMenu();
		}
	}
	if (ImGui::BeginMenu("パラメータ")) {
		for (const ShaderGraphParameter& parameter : editSession_.GetDraft().parameters) {

			if (ImGui::MenuItem(parameter.name.c_str())) {

				AddParameterNode(parameter.id, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("キーワード")) {
		for (const ShaderGraphKeyword& keyword : editSession_.GetDraft().keywords) {
			if (ImGui::MenuItem(keyword.name.c_str())) {
				AddKeywordNode(keyword.id, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	ImGui::EndPopup();
}

void Engine::ShaderGraphCanvasMenu::DuplicateNode(UUID nodeID) {

	// 元のNodeを複製してCanvasへ配置する
	nodeTransfer_.DuplicateNode(nodeID);
}

void Engine::ShaderGraphCanvasMenu::AddNode(ShaderGraphNodeKind kind, Vector2 position) {

	// 追加したNodeを指定位置へ配置する
	PlaceCreatedNode(nodeCreation_.AddNode(kind, position), position);
}

void Engine::ShaderGraphCanvasMenu::AddConstantNode(ShaderGraphValueType type, Vector2 position) {

	// 追加したNodeを指定位置へ配置する
	PlaceCreatedNode(nodeCreation_.AddConstantNode(type, position), position);
}

void Engine::ShaderGraphCanvasMenu::AddParameterNode(UUID parameterID, Vector2 position) {

	// 追加したNodeを指定位置へ配置する
	PlaceCreatedNode(nodeCreation_.AddParameterNode(parameterID, position), position);
}

void Engine::ShaderGraphCanvasMenu::AddKeywordNode(UUID keywordID, Vector2 position) {

	// 追加したNodeを指定位置へ配置する
	PlaceCreatedNode(nodeCreation_.AddKeywordNode(keywordID, position), position);
}

void Engine::ShaderGraphCanvasMenu::PlaceCreatedNode(UUID nodeID, Vector2 position) {

	if (!nodeID) {
		return;
	}
	// 作成結果をNode Editorの配置へ反映する
	ed::SetNodePosition(ed::NodeId(ToNodeEditorID(nodeID)), ImVec2(position.x, position.y));
}
