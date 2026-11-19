#include "ShaderGraphGenericNodeInspector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include "ShaderGraphEditOperations.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>

// c++
#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>

namespace {

	using namespace Engine;

	// 未接続入力の上書き値を保存する
	void StoreInput(ShaderGraphEditSession& session, ShaderGraphNode& node, uint32_t slot,
		ShaderGraphValueType type, const MaterialParameterValue& value) {

		const auto* descriptor = ShaderGraphNodeRegistry::Find(node.kind);
		while (node.inputPorts.size() < descriptor->inputs.size()) {
			const auto i = static_cast<uint32_t>(node.inputPorts.size());
			const auto defaults = GetShaderGraphInputDefault(node, i);
			node.inputPorts.push_back({ Engine::UUID::New(), std::string(descriptor->inputs[i].name), defaults.type, {}, false });
		}
		node.inputPorts[slot].type = type;
		node.inputPorts[slot].defaultValue = value;
		node.inputPorts[slot].hasDefault = true;
		session.MarkDirty();
	}

	// 汎用ノードの未接続入力を編集する
	void DrawInputs(ShaderGraphEditSession& session, ShaderGraphNode& node) {

		if (node.kind < ShaderGraphNodeKind::Exponential) { return; }
		const auto* descriptor = ShaderGraphNodeRegistry::Find(node.kind);
		if (!descriptor || descriptor->inputs.empty()) { return; }
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphGenericInputs");
		for (uint32_t i = 0; i < descriptor->inputs.size(); ++i) {
			if (node.kind == ShaderGraphNodeKind::DielectricSpecular) {
				const auto material = node.settings.dielectricMaterial;
				if ((i == 0 && material != ShaderGraphDielectricMaterial::Common) ||
					(i == 1 && material != ShaderGraphDielectricMaterial::Custom)) { continue; }
			}
			const auto& descriptorPort = descriptor->inputs[i];
			const auto defaults = GetShaderGraphInputDefault(node, i);
			const auto type = defaults.type;
			if (type == ShaderGraphValueType::Texture2D || type == ShaderGraphValueType::SamplerState ||
				type == ShaderGraphValueType::Gradient || type == ShaderGraphValueType::Matrix2 ||
				type == ShaderGraphValueType::Matrix3 || type == ShaderGraphValueType::Matrix4) { continue; }
			const bool connected = std::any_of(session.GetDraft().links.begin(), session.GetDraft().links.end(),
				[&](const ShaderGraphLink& link) { return link.inputNode == node.id && link.inputSlot == i; });
			if (connected) { continue; }
			ImGui::PushID(static_cast<int>(i));
			const bool overridden = i < node.inputPorts.size() && node.inputPorts[i].hasDefault;
			auto value = overridden ? node.inputPorts[i].defaultValue : defaults.value;
			bool changed = false;
			const std::string label(descriptorPort.name);
			if (!overridden && !defaults.binding.empty()) {
				bool fixed = false;
				if (MyGUI::BeginPropertyRow(label.c_str())) {
					ImGui::TextUnformatted("組込み入力");
					ImGui::SameLine();
					fixed = ImGui::SmallButton("固定値で編集");
					MyGUI::EndPropertyRow();
				}
				if (fixed) {
					StoreInput(session, node, i, type, ShaderGraphEditOperations::DefaultValueForGraphType(type));
				}
				ImGui::PopID();
				continue;
			}
			std::visit([&](auto& current) {
				using T = std::decay_t<decltype(current)>;
				if constexpr (std::is_same_v<T, float>) { changed = MyGUI::DragFloat(label.c_str(), current).valueChanged; }
				else if constexpr (std::is_same_v<T, Vector2>) { changed = MyGUI::DragVector2(label.c_str(), current).valueChanged; }
				else if constexpr (std::is_same_v<T, Vector3>) { changed = MyGUI::DragVector3(label.c_str(), current).valueChanged; }
				else if constexpr (std::is_same_v<T, Vector4>) { changed = MyGUI::DragVector4(label.c_str(), current).valueChanged; }
				else if constexpr (std::is_same_v<T, bool>) { changed = MyGUI::Checkbox(label.c_str(), current); }
				else if constexpr (std::is_same_v<T, int32_t>) { changed = MyGUI::DragInt(label.c_str(), current).valueChanged; }
			}, value.value);
			if (changed) {
				StoreInput(session, node, i, type, value);
			}
			if (overridden) {
				ImGui::SameLine();
				if (ImGui::SmallButton("戻す")) { node.inputPorts[i].hasDefault = false; session.MarkDirty(); }
			}
			ImGui::PopID();
		}
	}

	// グラデーションの色と不透明度を編集する
	void DrawGradient(ShaderGraphEditSession& session, ShaderGraphNodeSettings& stored) {

		auto settings = stored;
		if (settings.gradientColors.empty()) { settings.gradientColors = { Vector4{ 0, 0, 0, 0 }, Vector4{ 1, 1, 1, 1 } }; }
		if (settings.gradientAlphas.empty()) { settings.gradientAlphas = { Vector2{ 1, 0 }, Vector2{ 1, 1 } }; }
		bool changed = false;
		changed |= (MyGUI::EnumCombo("Mode", settings.gradientMode).valueChanged);
		for (size_t i = 0; i < settings.gradientColors.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			auto& key = settings.gradientColors[i];
			Color3 color{ key.x, key.y, key.z };
			if (MyGUI::ColorEdit("Color", color).valueChanged) { key.x = color.r; key.y = color.g; key.z = color.b; changed = true; }
			changed |= (MyGUI::DragFloat("Location", key.w, { .minValue = 0, .maxValue = 1 }).valueChanged);
			if (1 < settings.gradientColors.size() && ImGui::SmallButton("色を削除")) {
				settings.gradientColors.erase(settings.gradientColors.begin() + static_cast<ptrdiff_t>(i));
				changed = true; ImGui::PopID(); break;
			}
			ImGui::PopID();
		}
		if (settings.gradientColors.size() < 8 && ImGui::Button("色を追加")) {
			settings.gradientColors.emplace_back(Vector4{ 1, 1, 1, 1 }); changed = true;
		}
		for (size_t i = 0; i < settings.gradientAlphas.size(); ++i) {
			ImGui::PushID(static_cast<int>(i + 16));
			auto& key = settings.gradientAlphas[i];
			changed |= (MyGUI::DragFloat("Alpha", key.x, { .minValue = 0, .maxValue = 1 }).valueChanged);
			changed |= (MyGUI::DragFloat("Location", key.y, { .minValue = 0, .maxValue = 1 }).valueChanged);
			if (1 < settings.gradientAlphas.size() && ImGui::SmallButton("不透明度を削除")) {
				settings.gradientAlphas.erase(settings.gradientAlphas.begin() + static_cast<ptrdiff_t>(i));
				changed = true; ImGui::PopID(); break;
			}
			ImGui::PopID();
		}
		if (settings.gradientAlphas.size() < 8 && ImGui::Button("不透明度を追加")) {
			settings.gradientAlphas.emplace_back(Vector2{ 1, 1 }); changed = true;
		}
		if (changed) { stored = std::move(settings); session.MarkDirty(); }
	}
}

void Engine::ShaderGraphGenericNodeInspector::Draw(ShaderGraphEditSession& session, ShaderGraphNode& node) {

	using Kind = ShaderGraphNodeKind;
	auto& settings = node.settings;
	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphGenericNodeSettings");
	switch (node.kind) {
	case Kind::Exponential:
	case Kind::Log: session.MarkDirty(MyGUI::EnumCombo("Base", settings.mathBase).valueChanged); break;
	case Kind::Comparison: session.MarkDirty(MyGUI::EnumCombo("Comparison", settings.comparison).valueChanged); break;
	case Kind::Blend: session.MarkDirty(MyGUI::EnumCombo("Blend", settings.blend).valueChanged); break;
	case Kind::DielectricSpecular: session.MarkDirty(MyGUI::EnumCombo("Material", settings.dielectricMaterial).valueChanged); break;
	case Kind::MetalReflectance: session.MarkDirty(MyGUI::EnumCombo("Material", settings.metalMaterial).valueChanged); break;
	case Kind::NormalBlend: session.MarkDirty(MyGUI::EnumCombo("Mode", settings.normalBlend).valueChanged); break;
	case Kind::NormalFromHeight: session.MarkDirty(MyGUI::EnumCombo("Space", settings.normalSpace).valueChanged); break;
	case Kind::Triplanar: session.MarkDirty(MyGUI::Checkbox("Normal Map", settings.normalMap)); break;
	case Kind::Hue:
	case Kind::RotateAboutAxis: session.MarkDirty(MyGUI::Checkbox("Degrees", settings.degrees)); break;
	case Kind::ColorspaceConversion:
		session.MarkDirty(MyGUI::EnumCombo("From", settings.colorFrom).valueChanged);
		session.MarkDirty(MyGUI::EnumCombo("To", settings.colorTo).valueChanged);
		break;
	case Kind::Transform:
		session.MarkDirty(MyGUI::EnumCombo("From", settings.spaceFrom).valueChanged);
		session.MarkDirty(MyGUI::EnumCombo("To", settings.spaceTo).valueChanged);
		session.MarkDirty(MyGUI::EnumCombo("型", settings.transformType).valueChanged);
		session.MarkDirty(MyGUI::Checkbox("Normalize", settings.normalize));
		break;
	case Kind::TransformationMatrix:
		session.MarkDirty(MyGUI::EnumCombo("Matrix", settings.transformationMatrix).valueChanged);
		break;
	case Kind::MatrixConstruction:
	case Kind::MatrixSplit: session.MarkDirty(MyGUI::EnumCombo("Axis", settings.matrixAxis).valueChanged); break;
	case Kind::InvertColors:
	case Kind::ChannelMask:
	case Kind::Flip:
		for (uint32_t i = 0; i < 4; ++i) {
			const std::string label(1, "RGBA"[i]);
			bool enabled = (settings.channelMask & (1u << i)) != 0;
			if (MyGUI::Checkbox(label.c_str(), enabled)) {
				settings.channelMask = enabled ? settings.channelMask | (1u << i) : settings.channelMask & ~(1u << i);
				session.MarkDirty();
			}
		}
		break;
	case Kind::Swizzle:
		for (uint32_t i = 0; i < 4; ++i) {
			int32_t channel = static_cast<int32_t>(settings.swizzle[i]);
			const std::string label(1, "RGBA"[i]);
			if (MyGUI::DragInt(label.c_str(), channel, { .minValue = 0, .maxValue = 3 }).valueChanged) {
				settings.swizzle[i] = static_cast<uint32_t>(channel); session.MarkDirty();
			}
		}
		break;
	default: break;
	}
	const auto matrixType = node.kind == Kind::Constant ? node.valueType :
		(node.kind == Kind::Matrix2 ? ShaderGraphValueType::Matrix2 : (node.kind == Kind::Matrix3 ? ShaderGraphValueType::Matrix3 :
			(node.kind == Kind::Matrix4 ? ShaderGraphValueType::Matrix4 : ShaderGraphValueType::Invalid)));
	if (matrixType == ShaderGraphValueType::Matrix2 || matrixType == ShaderGraphValueType::Matrix3 ||
		matrixType == ShaderGraphValueType::Matrix4) {
		session.MarkDirty(MyGUI::EnumCombo("Axis", settings.matrixAxis).valueChanged);
		const uint32_t width = matrixType == ShaderGraphValueType::Matrix2 ? 2 : (matrixType == ShaderGraphValueType::Matrix3 ? 3 : 4);
		for (uint32_t i = 0; i < width; ++i) {
			const auto label = std::string(settings.matrixAxis == ShaderGraphMatrixAxis::Row ? "Row " : "Column ") + std::to_string(i);
			auto& row = settings.matrixRows[i];
			if (width == 2) {
				Vector2 value{ row.x, row.y };
				if (MyGUI::DragVector2(label.c_str(), value).valueChanged) {
					row.x = value.x; row.y = value.y; session.MarkDirty();
				}
			} else if (width == 3) {
				Vector3 value{ row.x, row.y, row.z };
				if (MyGUI::DragVector3(label.c_str(), value).valueChanged) {
					row.x = value.x; row.y = value.y; row.z = value.z; session.MarkDirty();
				}
			} else {
				session.MarkDirty(MyGUI::DragVector4(label.c_str(), row).valueChanged);
			}
		}
	}
	if (node.kind == Kind::Gradient || (node.kind == Kind::Constant && node.valueType == ShaderGraphValueType::Gradient)) {
		DrawGradient(session, settings);
	}
	DrawInputs(session, node);
}
