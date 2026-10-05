#include "ShaderGraphParameterEditor.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditOperations.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <string>
#include <utility>
#include <variant>
// imgui
#include <imgui.h>

using Engine::ShaderGraphEditOperations::DefaultValueForGraphType;

void Engine::ShaderGraphParameterEditor::Draw(const EditorToolContext& context, ShaderGraphEditSession& session) {

	// 一覧から編集するParameterを選ぶ
	ImGui::SeparatorText("公開パラメータ");
	for (uint32_t index = 0; index < session.GetDraft().parameters.size(); ++index) {

		const ShaderGraphParameter& parameter = session.GetDraft().parameters[index];
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable(parameter.name.c_str(), selected_ == static_cast<int32_t>(index))) {

			selected_ = static_cast<int32_t>(index);
		}
		ImGui::PopID();
	}

	const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("追加", ImVec2(buttonWidth, 0.0f))) {

		const uint32_t suffix = static_cast<uint32_t>(session.GetDraft().parameters.size() + 1);
		ShaderGraphParameter parameter{
			.id = UUID::New(),
			.name = "Parameter" + std::to_string(suffix),
			.type = ShaderGraphValueType::Float,
			.defaultValue = DefaultValueForGraphType(ShaderGraphValueType::Float),
		};
		session.GetDraft().parameters.emplace_back(std::move(parameter));
		selected_ = static_cast<int32_t>(session.GetDraft().parameters.size() - 1);
		session.MarkDirty();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(selected_ < 0 || static_cast<size_t>(selected_) >= session.GetDraft().parameters.size());
	if (ImGui::Button("削除", ImVec2(buttonWidth, 0.0f))) {

		Remove(session, static_cast<uint32_t>(selected_));
	}
	ImGui::EndDisabled();

	if (0 <= selected_ && static_cast<size_t>(selected_) < session.GetDraft().parameters.size()) {

		ImGui::SeparatorText("パラメータ設定");
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphParameterSettings");
		DrawValue(context, session, session.GetDraft().parameters[static_cast<size_t>(selected_)]);
	}
}

void Engine::ShaderGraphParameterEditor::DrawValue(
	const EditorToolContext& context, ShaderGraphEditSession& session, ShaderGraphParameter& parameter) {

	const std::string parameterID = ToString(parameter.id);
	ImGui::TextDisabled("ID: %s", parameterID.c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton("コピー##ParameterID")) {
		ImGui::SetClipboardText(parameterID.c_str());
		session.GetStatusMessage() = "パラメータIDをコピーしました";
	}

	session.MarkDirty(MyGUI::InputText("名前", parameter.name).valueChanged);
	session.MarkDirty(MyGUI::InputText("参照名", parameter.referenceName).valueChanged);
	session.MarkDirty(MyGUI::EnumCombo("精度", parameter.precision).valueChanged);
	session.MarkDirty(MyGUI::EnumCombo("更新単位", parameter.scope).valueChanged);
	session.MarkDirty(MyGUI::Checkbox("公開", parameter.exposed));

	const ShaderGraphValueType oldType = parameter.type;
	if (MyGUI::EnumCombo("型", parameter.type).valueChanged) {

		if (parameter.type == ShaderGraphValueType::Invalid || parameter.type == ShaderGraphValueType::SamplerState) {
			parameter.type = oldType;
		} else {
			parameter.defaultValue = DefaultValueForGraphType(parameter.type);
			for (ShaderGraphNode& node : session.GetDraft().nodes) {
				if (node.kind == ShaderGraphNodeKind::Parameter && node.parameterID == parameter.id) {

					node.valueType = parameter.type;
				}
			}
			session.MarkDirty();
		}
	}
	session.MarkDirty(MyGUI::EnumCombo("Semantic", parameter.semantic).valueChanged);

	switch (parameter.type) {
	case ShaderGraphValueType::Float: {
		float value = std::get_if<float>(&parameter.defaultValue.value) ? std::get<float>(parameter.defaultValue.value) : 0.0f;
		if (MyGUI::DragFloat("既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float2: {
		Vector2 value =
			std::get_if<Vector2>(&parameter.defaultValue.value) ? std::get<Vector2>(parameter.defaultValue.value) : Vector2{};
		if (MyGUI::DragVector2("既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float3: {
		Vector3 value =
			std::get_if<Vector3>(&parameter.defaultValue.value) ? std::get<Vector3>(parameter.defaultValue.value) : Vector3{};
		if (MyGUI::DragVector3("既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float4: {
		Vector4 value =
			std::get_if<Vector4>(&parameter.defaultValue.value) ? std::get<Vector4>(parameter.defaultValue.value) : Vector4{};
		if (MyGUI::DragVector4("既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Color: {
		Color4 value = std::get_if<Color4>(&parameter.defaultValue.value) ? std::get<Color4>(parameter.defaultValue.value)
																		  : Color4(1.0f, 1.0f, 1.0f, 1.0f);
		if (MyGUI::ColorEdit("既定値", value, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs).valueChanged) {

			parameter.defaultValue.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Texture2D: {
		AssetID value =
			std::get_if<AssetID>(&parameter.defaultValue.value) ? std::get<AssetID>(parameter.defaultValue.value) : AssetID{};
		AssetEditSetting setting{};
		setting.graphicsCore = context.panelContext ? context.panelContext->graphicsCore : nullptr;
		if (MyGUI::AssetReferenceField("既定値", value, context.toolContext.assetDatabase, {AssetType::Texture}, setting)
				.valueChanged) {

			parameter.defaultValue.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Boolean: {
		bool value = std::get_if<bool>(&parameter.defaultValue.value) ? std::get<bool>(parameter.defaultValue.value) : false;
		if (MyGUI::Checkbox("既定値", value)) {
			parameter.defaultValue.value = value;
			session.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Integer: {
		int32_t value =
			std::get_if<int32_t>(&parameter.defaultValue.value) ? std::get<int32_t>(parameter.defaultValue.value) : 0;
		if (MyGUI::DragInt("既定値", value).valueChanged) {
			parameter.defaultValue.value = value;
			session.MarkDirty();
		}
		break;
	}
	default:
		break;
	}
}

void Engine::ShaderGraphParameterEditor::Remove(ShaderGraphEditSession& session, uint32_t index) {

	if (session.GetDraft().parameters.size() <= index) {
		return;
	}
	// Parameterを参照するNodeと接続を取り除く
	const UUID parameterID = session.GetDraft().parameters[index].id;
	std::vector<UUID> nodes;
	for (const ShaderGraphNode& node : session.GetDraft().nodes) {
		if (node.kind == ShaderGraphNodeKind::Parameter && node.parameterID == parameterID) {

			nodes.emplace_back(node.id);
		}
	}
	for (UUID nodeID : nodes) {
		ShaderGraphEditOperations::RemoveNode(session.GetDraft(), nodeID);
	}
	session.GetDraft().parameters.erase(session.GetDraft().parameters.begin() + index);
	selected_ = -1;
	session.MarkDirty();
}

void Engine::ShaderGraphParameterEditor::ResetSelection() {

	// 切替前のGraphの選択を持ち越さない
	selected_ = -1;
}
