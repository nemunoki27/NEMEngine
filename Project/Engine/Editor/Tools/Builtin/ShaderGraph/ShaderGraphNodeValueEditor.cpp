#include "ShaderGraphNodeValueEditor.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditOperations.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cfloat>
#include <variant>
// imgui
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>

using Engine::ShaderGraphEditOperations::DefaultValueForGraphType;

namespace {

	namespace ed = ax::NodeEditor;
	constexpr const char* kNodeValuePopup = "ShaderGraphNodeValue";

	// Node幅に合わせた入力欄を作る
	Engine::PropertyRowSetting NodeValueRowSetting(const char* label, float rowWidth) {

		return Engine::PropertyRowSetting{
			.labelWidth = ImGui::CalcTextSize(label).x + 4.0f,
			.rowWidth = rowWidth,
		};
	}
} // namespace

void Engine::ShaderGraphNodeValueEditor::DrawValue(ShaderGraphEditSession& session, ShaderGraphNode& node, float nodeWidth) {

	if (node.kind == ShaderGraphNodeKind::Constant) {
		DrawNodeValueTypeButton(node, nodeWidth);
	}

	const ShaderGraphValueType valueType =
		node.kind == ShaderGraphNodeKind::TextureSample ? ShaderGraphValueType::Color : node.valueType;
	switch (valueType) {
	case ShaderGraphValueType::Float: {
		float value = std::get_if<float>(&node.value.value) ? std::get<float>(node.value.value) : 0.0f;
		FloatEditSetting setting{};
		setting.propertyRow = NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragFloat("値", value, setting).valueChanged) {

			node.value.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float2: {
		Vector2 value = std::get_if<Vector2>(&node.value.value) ? std::get<Vector2>(node.value.value) : Vector2{};
		FloatEditSetting setting{};
		setting.propertyRow = NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector2("値", value, setting).valueChanged) {

			node.value.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float3: {
		Vector3 value = std::get_if<Vector3>(&node.value.value) ? std::get<Vector3>(node.value.value) : Vector3{};
		FloatEditSetting setting{};
		setting.propertyRow = NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector3("値", value, setting).valueChanged) {

			node.value.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float4: {
		Vector4 value = std::get_if<Vector4>(&node.value.value) ? std::get<Vector4>(node.value.value) : Vector4{};
		FloatEditSetting setting{};
		setting.propertyRow = NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector4("値", value, setting).valueChanged) {

			node.value.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Color: {
		Color4 value = std::get_if<Color4>(&node.value.value) ? std::get<Color4>(node.value.value) : Color4::White();
		const char* label = node.kind == ShaderGraphNodeKind::TextureSample ? "未設定時" : "値";
		DrawNodeColorButton(node, label, value, nodeWidth);
		break;
	}
	case ShaderGraphValueType::Boolean: {
		bool value = std::get_if<bool>(&node.value.value) ? std::get<bool>(node.value.value) : false;
		if (MyGUI::Checkbox("値", value, NodeValueRowSetting("値", nodeWidth))) {
			node.value.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Integer: {
		int32_t value = std::get_if<int32_t>(&node.value.value) ? std::get<int32_t>(node.value.value) : 0;
		IntEditSetting setting{};
		setting.propertyRow = NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragInt("値", value, setting).valueChanged) {
			node.value.value = value;
			session.MarkDirty();
		}
		break;
	}
	default:
		break;
	}
}

void Engine::ShaderGraphNodeValueEditor::RequestNodeValuePopup(
	UUID nodeID, NodeValuePopupKind kind, const Vector2& anchor, float width, uint32_t viewportID) {

	nodeValuePopupNode_ = nodeID;
	nodeValuePopupKind_ = kind;
	nodeValuePopupAnchor_ = anchor;
	nodeValuePopupWidth_ = width;
	nodeValuePopupViewportID_ = viewportID;
	requestNodeValuePopup_ = true;
}

void Engine::ShaderGraphNodeValueEditor::DrawNodeValueTypeButton(ShaderGraphNode& node, float nodeWidth) {

	if (!MyGUI::BeginPropertyRow("型", NodeValueRowSetting("型", nodeWidth))) {

		return;
	}
	const float width = (std::max)(ImGui::GetContentRegionAvail().x, 1.0f);
	ImGui::SetNextItemWidth(width);
	ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
	const bool pressed =
		ImGui::Button(EnumAdapter<ShaderGraphValueType>::ToString(node.valueType), ImVec2(width, ImGui::GetFrameHeight()));
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(3);

	const ImVec2 itemMin = ImGui::GetItemRectMin();
	const ImVec2 itemMax = ImGui::GetItemRectMax();
	const ImVec2 screenMin = ed::CanvasToScreen(itemMin);
	const ImVec2 screenMax = ed::CanvasToScreen(itemMax);
	const ImVec2 arrowPosition(
		itemMax.x - ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x, itemMin.y + ImGui::GetStyle().FramePadding.y);
	ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrowPosition, ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Down);
	if (pressed) {

		RequestNodeValuePopup(node.id, NodeValuePopupKind::ValueType, Vector2(screenMin.x, screenMax.y),
			screenMax.x - screenMin.x, ImGui::GetWindowViewport()->ID);
	}
	MyGUI::EndPropertyRow();
}

void Engine::ShaderGraphNodeValueEditor::DrawNodeColorButton(
	const ShaderGraphNode& node, const char* label, const Color4& value, float nodeWidth) {

	if (!MyGUI::BeginPropertyRow(label, NodeValueRowSetting(label, nodeWidth))) {

		return;
	}
	const ImVec4 color(value.r, value.g, value.b, value.a);
	const bool pressed = ImGui::ColorButton("##Value", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
	const ImVec2 itemMin = ImGui::GetItemRectMin();
	const ImVec2 itemMax = ImGui::GetItemRectMax();
	const ImVec2 screenMin = ed::CanvasToScreen(itemMin);
	const ImVec2 screenMax = ed::CanvasToScreen(itemMax);
	if (pressed) {

		RequestNodeValuePopup(node.id, NodeValuePopupKind::Color,
			Vector2(screenMin.x - 1.0f, screenMax.y + ImGui::GetStyle().ItemSpacing.y), 0.0f, ImGui::GetWindowViewport()->ID);
	}
	MyGUI::EndPropertyRow();
}

void Engine::ShaderGraphNodeValueEditor::DrawPopup(ShaderGraphEditSession& session) {

	if (requestNodeValuePopup_) {
		ImGui::OpenPopup(kNodeValuePopup);
		requestNodeValuePopup_ = false;
	}
	if (ImGui::IsPopupOpen(kNodeValuePopup)) {
		ImGui::SetNextWindowViewport(nodeValuePopupViewportID_);
		ImGui::SetNextWindowPos(ImVec2(nodeValuePopupAnchor_.x, nodeValuePopupAnchor_.y), ImGuiCond_Appearing);
		if (nodeValuePopupKind_ == NodeValuePopupKind::ValueType) {

			ImGui::SetNextWindowSizeConstraints(ImVec2(nodeValuePopupWidth_, 0.0f),
				ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 8.0f + ImGui::GetStyle().WindowPadding.y * 2.0f));
		}
	}

	if (!ImGui::BeginPopup(kNodeValuePopup)) {
		if (!ImGui::IsPopupOpen(kNodeValuePopup)) {
			nodeValuePopupNode_ = UUID{};
			nodeValuePopupKind_ = NodeValuePopupKind::None;
		}
		return;
	}

	const auto found = std::find_if(session.GetDraft().nodes.begin(), session.GetDraft().nodes.end(),
		[&](const ShaderGraphNode& node) { return node.id == nodeValuePopupNode_; });
	if (found == session.GetDraft().nodes.end()) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	ShaderGraphNode& node = *found;
	switch (nodeValuePopupKind_) {
	case NodeValuePopupKind::ValueType:
		for (uint32_t index = 0; index < EnumAdapter<ShaderGraphValueType>::GetEnumCount(); ++index) {

			const ShaderGraphValueType valueType = EnumAdapter<ShaderGraphValueType>::GetValue(index);
			if (valueType == ShaderGraphValueType::Invalid || valueType == ShaderGraphValueType::Texture2D ||
				valueType == ShaderGraphValueType::SamplerState) {

				continue;
			}
			if (ImGui::Selectable(EnumAdapter<ShaderGraphValueType>::ToString(valueType), node.valueType == valueType)) {

				node.valueType = valueType;
				node.value = DefaultValueForGraphType(valueType);
				session.MarkDirty();
				ImGui::CloseCurrentPopup();
			}
		}
		break;
	case NodeValuePopupKind::Color: {
		Color4 value = std::get_if<Color4>(&node.value.value) ? std::get<Color4>(node.value.value) : Color4::White();
		float color[4] = {value.r, value.g, value.b, value.a};
		ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 12.0f);
		if (ImGui::ColorPicker4("##NodeColorPicker", color,
				ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreviewHalf)) {

			node.value.value = Color4(color[0], color[1], color[2], color[3]);
			session.MarkDirty();
		}
		break;
	}
	case NodeValuePopupKind::None:
	default: ImGui::CloseCurrentPopup();
		break;
	}
	ImGui::EndPopup();
}

void Engine::ShaderGraphNodeValueEditor::Reset() {

	// 開いていたGraphへの参照を解除する
	nodeValuePopupNode_ = UUID{};
	nodeValuePopupKind_ = NodeValuePopupKind::None;
	nodeValuePopupAnchor_ = Vector2{};
	nodeValuePopupWidth_ = 0.0f;
	nodeValuePopupViewportID_ = 0;
	requestNodeValuePopup_ = false;
}
