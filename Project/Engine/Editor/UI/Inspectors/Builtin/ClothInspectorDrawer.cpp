#include "ClothInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorPalette.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorPresentation.h>

using namespace Engine;

void ClothInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const ClothComponent&) {

	const auto* record = TryGetCloth(world, entity);
	settings_ = record ? record->settings : ClothSettings{};
}

void ClothInspectorDrawer::SerializeDraft(ECSWorld&, const Entity&, const ClothComponent&, nlohmann::json& out) const {

	out = settings_;
}

void ClothInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity, const ClothComponent&) {

	SetClothSettings(world, entity, settings_);
}

void ClothInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) {

	const auto* renderer = world.TryGetComponent<PrimitiveRendererComponent>(entity);
	if (!IsClothPlane(renderer)) {
		ImGui::PushStyleColor(ImGuiCol_Text, Engine::EditorPalette::Get("Panels.warning"));
		MyGUI::Text("PrimitiveRendererで3DPlaneを設定してください");
		ImGui::PopStyleColor();
		return;
	}
	auto& s = settings_;
	const auto field = [&](ValueEditResult edit) { PushEditResult(edit, active); };
	field(MyGUI::CheckboxField("有効", s.enabled));
	ImGui::SeparatorText("プレビュー");
	field(MyGUI::CheckboxField("編集中に再生", s.playInEditMode));

	ImGui::SeparatorText("固定位置");
	field(MyGUI::EnumCombo("固定方式", s.pinMode));
	field(MyGUI::EnumCombo("固定辺", s.edge));
	if (s.pinMode == ClothPinMode::Fasteners) {
		field(MyGUI::DragInt("留め具の数", s.fasteners, {.minValue = 2, .maxValue = kMaxPrimitiveDivide + 1}));
	}
	field(MyGUI::DragFloat("ひだの深さ", s.foldDepth, {.dragSpeed = 0.001f, .minValue = 0.0f}));

	ImGui::SeparatorText("布の物性");
	// 柔らかさを百万倍して小さな値もプロパティ行で調整できるようにする
	const auto compliance = [&](const char* label, float& value) {
		float scaled = value * 1.0e6f;
		const auto edit = MyGUI::DragFloat(label, scaled, {.dragSpeed = 0.25f, .minValue = 0.0f, .maxValue = 1.0e6f});
		if (edit.valueChanged) value = scaled * 1.0e-6f;
		field(edit);
	};
	compliance("伸びやすさ", s.stretchCompliance);
	compliance("ずれやすさ", s.shearCompliance);
	compliance("曲がりやすさ", s.bendCompliance);
	field(MyGUI::DragFloat("減衰", s.damping, {.minValue = 0.0f}));
	field(MyGUI::DragFloat("重力倍率", s.gravityScale));

	ImGui::SeparatorText("風");
	field(MyGUI::CheckboxField("風を有効", s.windEnabled));
	field(MyGUI::CheckboxField("周囲の風の影響を受ける", s.receiveSceneWind));
	if (s.receiveSceneWind) field(DrawInspectorMask(context, "風の衝突レイヤー", s.windCollisionMask, InspectorMaskDomain::Collision));
	ImGui::BeginDisabled(!s.windEnabled);
	field(MyGUI::DragVector3("風向き", s.windDirection));
	field(MyGUI::DragFloat("風の強さ", s.windStrength, {.minValue = 0.0f}));
	field(MyGUI::DragFloat("突風の強さ", s.gustStrength, {.minValue = 0.0f}));
	field(MyGUI::DragFloat("突風の速さ", s.gustFrequency, {.minValue = 0.0f}));

	ImGui::EndDisabled();

	ImGui::SeparatorText("衝突");
	field(DrawInspectorMask(context, "衝突レイヤー", s.collisionMask, InspectorMaskDomain::Collision));
	field(MyGUI::DragFloat("厚さ", s.thickness, {.dragSpeed = 0.001f, .minValue = 0.0f}));
}
