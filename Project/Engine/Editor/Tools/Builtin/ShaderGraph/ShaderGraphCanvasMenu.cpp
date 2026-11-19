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
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>

// c++
#include <algorithm>
#include <array>
#include <cfloat>
#include <string_view>
#include <vector>
// imgui
#include <imgui.h>
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

void Engine::ShaderGraphCanvasMenu::Draw(ImTextureID searchIcon) {

	if (ImGui::BeginPopup(kNodeContextPopup)) {
		const auto group = std::find_if(editSession_.GetDraft().groups.begin(), editSession_.GetDraft().groups.end(),
			[&](const ShaderGraphGroup& value) { return value.id == contextNode_; });
		const auto node = std::find_if(editSession_.GetDraft().nodes.begin(), editSession_.GetDraft().nodes.end(),
			[&](const ShaderGraphNode& value) { return value.id == contextNode_; });

		if (group != editSession_.GetDraft().groups.end()) {
			ImGui::TextUnformatted(group->name.c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("Duplicate Group")) {
				groupEditor_.DuplicateGroup(contextNode_);
			}
			if (ImGui::MenuItem("Delete Group")) {
				groupEditor_.RemoveGroup(contextNode_);
			}
		} else {
			if (node != editSession_.GetDraft().nodes.end()) {
				ImGui::TextUnformatted(GetShaderGraphNodeName(node->kind).data());
			}
			ImGui::Separator();
			if (node != editSession_.GetDraft().nodes.end() && IsPreviewableNode(node->kind)) {

				if (ImGui::MenuItem("Show Preview", nullptr, node->previewExpanded)) {

					node->previewExpanded = !node->previewExpanded;
					editSession_.MarkDirty();
				}
				ImGui::Separator();
			}
			ImGui::BeginDisabled(
				node == editSession_.GetDraft().nodes.end() || contextNode_ == editSession_.GetDraft().outputNode);
			if (ImGui::MenuItem("Duplicate Node")) {
				DuplicateNode(contextNode_);
			}
			if (ImGui::MenuItem("Delete Node")) {
				editSession_.RemoveNode(contextNode_);
			}
			ImGui::EndDisabled();

			const bool canGroup = 2 <= canvas_.GetSelectedNodes(editSession_.GetDraft()).size();
			ImGui::BeginDisabled(!canGroup);
			if (ImGui::MenuItem("選択項目をグループ化")) {

				groupEditor_.GroupSelectedNodes(canvas_.GetSelectedNodes(editSession_.GetDraft()));
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}

	DrawNodeCreationMenu(searchIcon);
}

void Engine::ShaderGraphCanvasMenu::DrawNodeCreationMenu(ImTextureID searchIcon) {

	if (ImGui::IsPopupOpen(kCreateNodePopup)) {
		ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
	}
	if (!ImGui::BeginPopup(kCreateNodePopup)) {
		return;
	}

	const bool canGroup = 2 <= canvas_.GetSelectedNodes(editSession_.GetDraft()).size();
	ImGui::BeginDisabled(!canGroup);
	if (ImGui::MenuItem("選択項目をグループ化")) {

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
	nodeSearch_.DrawInput("##NodeSearch", searchIcon, "ノードを検索...");
	if (editSession_.GetDraft().domain == ShaderGraphDomain::Surface &&
		SupportsShaderGraphVertexOutput(editSession_.GetDraft().target) && !editSession_.GetDraft().vertexOutputNode &&
		ImGui::MenuItem("頂点出力を追加")) {

		AddNode(ShaderGraphNodeKind::VertexOutput, createNodePosition_);
	}
	if (ImGui::BeginMenu("Constant")) {
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
			const char* typeName = ImGuiUtility::GetEnumDisplayName(magic_enum::enum_type_name<ShaderGraphValueType>(),
				EnumAdapter<ShaderGraphValueType>::ToString(type));
			if (ImGui::MenuItem(typeName)) {
				AddConstantNode(type, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}

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
	if (nodeSearch_.IsActive()) {
		for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
			if (!isCreatable(descriptor)) {
				continue;
			}
			const std::string searchable = std::string(descriptor.name) + " " + std::string(descriptor.category) + " " +
				ImGuiUtility::GetDisplayName(descriptor.name.data());
			if (nodeSearch_.Matches(searchable) && ImGui::MenuItem(descriptor.name.data())) {
				AddNode(descriptor.kind, createNodePosition_);
			}
		}
	} else {
		const auto drawCategory = [&](const auto& draw, std::string_view parent) -> void {
			std::vector<std::string_view> categories;
			for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
				if (!isCreatable(descriptor)) continue;
				if (descriptor.category == parent) {
					if (ImGui::MenuItem(descriptor.name.data())) AddNode(descriptor.kind, createNodePosition_);
					continue;
				}
				if (!parent.empty() && (!descriptor.category.starts_with(parent) ||
					descriptor.category.size() <= parent.size() || descriptor.category[parent.size()] != '/')) continue;
				const auto remaining = descriptor.category.substr(parent.empty() ? 0 : parent.size() + 1);
				const auto category = remaining.substr(0, remaining.find('/'));
				if (std::find(categories.begin(), categories.end(), category) == categories.end()) categories.emplace_back(category);
			}
			for (std::string_view category : categories) {
				const std::string label(category);
				if (!ImGui::BeginMenu(label.c_str())) continue;
				const std::string path = parent.empty() ? label : std::string(parent) + "/" + label;
				draw(draw, path);
				ImGui::EndMenu();
			}
		};
		drawCategory(drawCategory, {});
	}
	if (ImGui::BeginMenu("Parameter")) {
		for (const ShaderGraphParameter& parameter : editSession_.GetDraft().parameters) {

			if (ImGui::MenuItem(parameter.name.c_str())) {

				AddParameterNode(parameter.id, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Keyword")) {
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
