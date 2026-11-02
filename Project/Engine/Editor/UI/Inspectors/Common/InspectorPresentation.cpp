#include "InspectorPresentation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Settings/ProjectRenderingLayerSettings.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>

// c++
#include <array>
#include <bit>

namespace {

	struct FieldDefinition { const char* key; const char* label; };
	constexpr std::array<FieldDefinition, 20> kFields{{
		FieldDefinition{"Enabled", "有効"}, {"Visible", "表示"}, {"DisplayLayer", "表示レイヤー"},
		{"CameraLayer", "描画対象レイヤー"}, {"ReceiverLayer", "受け取り先レイヤー"}, {"ParticleGroup", "作用グループ"},
		{"SortLayer", "ソートレイヤー"}, {"DrawOrder", "描画順"}, {"BlendMode", "ブレンドモード"}, {"RenderQueue", "描画キュー"},
		{"PlayOnStart", "開始時に再生"}, {"PlayInEditMode", "編集中も再生"}, {"EditorPreview", "編集中にプレビュー"},
		{"UnscaledTime", "TimeScaleの影響を受けない"}, {"Loop", "ループ再生"}, {"PlaybackSpeed", "再生速度"},
		{"Restitution", "はね返り率"}, {"Friction", "摩擦"}, {"AllowTopple", "支えが外れたら倒れる"}, {"AlphaThreshold", "アルファ閾値"}
	}};
	struct CategoryDefinition { std::string_view key; const char* label; };
	constexpr std::array<CategoryDefinition, 10> kCategories{{
		CategoryDefinition{"Core", "基本"}, {"Rendering", "描画"}, {"Particle", "パーティクル"}, {"Camera", "カメラ"},
		{"Lighting", "照明"}, {"Physics", "物理"}, {"UI", "UI"}, {"Audio", "音声"}, {"Animation", "アニメーション"}, {"Scripting", "スクリプト"}
	}};
}

Engine::InspectorIndentScope::InspectorIndentScope(const char* id) : hasID_(id != nullptr) {

	ImGui::Indent();
	if (hasID_) ImGui::PushID(id);
}

Engine::InspectorIndentScope::~InspectorIndentScope() {

	if (hasID_) ImGui::PopID();
	ImGui::Unindent();
}

const char* Engine::GetInspectorLabel(InspectorFieldID field) { return kFields.at(static_cast<size_t>(field)).label; }
const char* Engine::GetInspectorFieldKey(InspectorFieldID field) { return kFields.at(static_cast<size_t>(field)).key; }

const char* Engine::GetInspectorCategoryLabel(std::string_view category) {

	for (const auto& value : kCategories) if (value.key == category) return value.label;
	return nullptr;
}

int Engine::GetInspectorCategoryOrder(std::string_view category) {

	for (size_t index = 0; index < kCategories.size(); ++index) if (kCategories[index].key == category) return static_cast<int>(index);
	return static_cast<int>(kCategories.size());
}

std::string_view Engine::GetInspectorComponentCategory(std::string_view component, std::string_view fallback) {

	if (component == "ParticleSystem" || component == "ParticlePaintReceiver" || component == "ForceField" ||
		component == "VectorField" || component == "HeightmapCollider") return "Particle";
	if (component == "FlipbookAnimation") return "Animation";
	return fallback;
}

Engine::ValueEditResult Engine::DrawInspectorMask(const EditorPanelContext& context, const char* label,
	uint32_t& value, InspectorMaskDomain domain, const char* id) {

	ValueEditResult result;
	if (!MyGUI::BeginPropertyRow(label, {.id = id})) return result;
	ImGui::PushID(id ? id : label);
	const uint32_t all = domain == InspectorMaskDomain::Rendering ? kRenderingLayerMaskBits : UINT32_MAX;
	const uint32_t defined = domain == InspectorMaskDomain::Rendering && context.renderingLayerSettings ?
		context.renderingLayerSettings->GetDefinedMask() : all;
	const uint32_t selected = value & defined;
	std::string preview = selected == 0 ? "なし" : selected == defined ? "すべて" : std::to_string(std::popcount(selected)) + "個選択";
	if (domain == InspectorMaskDomain::Rendering && std::popcount(selected) == 1 && context.renderingLayerSettings)
		preview = context.renderingLayerSettings->GetNames()[std::countr_zero(selected)];
	if (ImGui::BeginCombo("##Mask", preview.c_str())) {
		if (ImGui::Selectable("すべて", defined != 0 && selected == defined)) { value = all; result.valueChanged = true; }
		if (ImGui::Selectable("なし", value == 0)) { value = 0; result.valueChanged = true; }
		ImGui::Separator();
		for (uint32_t index = 0; index < 32; ++index) {
			std::string name;
			if (domain == InspectorMaskDomain::Rendering) {
				if (!context.renderingLayerSettings || index >= ProjectRenderingLayerSettings::kLayerCount) continue;
				name = context.renderingLayerSettings->GetNames()[index];
				if (name.empty()) continue;
			} else if (domain == InspectorMaskDomain::Collision) {
				const auto& types = CollisionSettings::GetInstance().GetTypes();
				name = index < types.size() ? types[index].name : "未定義 " + std::to_string(index);
			} else name = "グループ " + std::to_string(index);
			ImGui::PushID(static_cast<int>(index));
			result.valueChanged |= ImGui::CheckboxFlags(name.c_str(), &value, uint32_t(1) << index);
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = result.valueChanged;
	ImGui::PopID();
	MyGUI::EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::DrawInspectorCommonFields(const EditorPanelContext& context,
	const InspectorCommonFields& fields) {

	ValueEditResult result;
	auto row = [&](InspectorFieldID id, auto&& draw) {
		ImGui::PushID(GetInspectorFieldKey(id));
		result.Merge(draw());
		ImGui::Separator();
		ImGui::PopID();
	};
	auto toggle = [&](InspectorFieldID id, bool* value) {
		if (!value) return;
		row(id, [&] { return MyGUI::CheckboxField(GetInspectorLabel(id), *value,
			{.id = GetInspectorFieldKey(id)}); });
	};
	auto mask = [&](InspectorFieldID id, uint32_t* value, InspectorMaskDomain domain) {
		if (!value) return;
		row(id, [&] { return DrawInspectorMask(context, GetInspectorLabel(id), *value, domain, GetInspectorFieldKey(id)); });
	};
	toggle(InspectorFieldID::Enabled, fields.enabled);
	toggle(InspectorFieldID::Visible, fields.visible);
	mask(InspectorFieldID::DisplayLayer, fields.displayLayer, InspectorMaskDomain::Rendering);
	if (fields.cameraLayer) row(InspectorFieldID::CameraLayer, [&] {
		uint32_t value = static_cast<uint32_t>(*fields.cameraLayer);
		auto edit = DrawInspectorMask(context, GetInspectorLabel(InspectorFieldID::CameraLayer), value,
			InspectorMaskDomain::Rendering, GetInspectorFieldKey(InspectorFieldID::CameraLayer));
		if (edit.valueChanged) *fields.cameraLayer = static_cast<int32_t>(value);
		return edit;
	});
	mask(InspectorFieldID::ReceiverLayer, fields.receiverLayer, InspectorMaskDomain::Rendering);
	mask(InspectorFieldID::ParticleGroup, fields.particleGroup, InspectorMaskDomain::Particle);
	return result;
}
