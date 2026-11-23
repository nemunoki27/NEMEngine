#include "BeamRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/RendererRenderSettingsFields.h>
#include <Engine/Editor/UI/Inspectors/Common/RibbonRendererFields.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>

// c++
#include <format>

using namespace Engine;

namespace {

	ValueEditResult DrawEndpoint(const char* label, BeamEndpoint& point, bool is2D, ECSWorld& world, Entity owner) {

		ValueEditResult result{};
		if (!MyGUI::CollapsingHeader(label)) return result;
		ImGui::PushID(label);
		result.Merge(MyGUI::EnumCombo("座標空間", point.mode));
		const bool attached = point.mode == BeamEndpointMode::Object;
		if (attached) {
			auto target = world.IsAlive(point.runtimeObject) ? world.GetUUID(point.runtimeObject) : Engine::UUID{};
			const auto edit = SceneEntityReferenceFields::Draw("オブジェクト", world, owner, point.object, {}, &target);
			if (edit.valueChanged) point.runtimeObject = target ? world.FindByUUID(target) : Entity::Null();
			result.Merge(edit);
		}
		const char* position = attached ? "オフセット" : "位置";
		if (is2D) {
			Vector2 xy(point.position.x, point.position.y);
			const auto edit = MyGUI::DragVector2(position, xy);
			if (edit.valueChanged) point.position = {xy.x, xy.y, point.position.z};
			result.Merge(edit);
		} else result.Merge(MyGUI::DragVector3(position, point.position));
		ImGui::PopID();
		return result;
	}
}

void BeamRendererInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const BeamRendererComponent&) {

	if (inspected_ != world.GetUUID(entity)) {
		inspected_ = world.GetUUID(entity);
		invalidMaterial_ = false;
	}
	const auto* record = TryGetBeamRenderer(world, entity);
	settings_ = record ? record->settings : BeamRendererSettings{};
}

void BeamRendererInspectorDrawer::SerializeDraft(ECSWorld&, const Entity&, const BeamRendererComponent&, nlohmann::json& out) const {

	out = settings_;
}

void BeamRendererInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity, const BeamRendererComponent&) {

	SetBeamRendererSettings(world, entity, settings_);
}

void BeamRendererInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) {

	auto& s = settings_;
	const auto field = [&](const ValueEditResult& result) { PushEditResult(result, active); };
	const auto number = [&](const char* label, float& value) {
		field(MyGUI::DragFloat(label, value, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f }));
	};

	field(DrawInspectorCommonFields(context, { .visible = &s.visible, .displayLayer = &s.renderingLayerMask }));
	field(RibbonRendererFields::DrawMaterial(context, s.material, invalidMaterial_, BuiltinAssets::Materials::DefaultBeam));
	field(MaterialParameterEditor::DrawReload(s.materialInstance));
	field(DrawRendererRenderSettingsFields(s.renderSettings));
	InspectorDrawerCommon::DrawCommonRenderFields(context, [&](auto&& f) { field(f()); }, s.layer, s.order, s.blendMode, s.queue);
	field(MyGUI::CheckboxField("2D描画", s.is2D));
	field(MyGUI::EnumCombo("種類", s.mode));
	materialParameters_.Draw(context, s.material, BuiltinAssets::Materials::DefaultBeam, s.materialInstance,
		[&](auto&& f) { field(f()); });

	ImGui::SeparatorText("ビームパラメータ");
	number("幅", s.width);
	number("幅の倍率", s.widthMultiplier);
	field(MyGUI::CheckboxField("幅カーブを使用", s.useWidthCurve));
	if (s.useWidthCurve) {
		InspectorIndentScope scope;
		RibbonRendererFields::DrawCurve("幅カーブ", s.widthCurve, widthState_, field);
	}
	{
		InspectorIndentScope scope;
		field(DrawEndpoint("始点", s.start, s.is2D, world, entity));
		if (MyGUI::CollapsingHeader("経由点")) {
			int remove = -1, moveFrom = -1, moveTo = -1;
			const int count = static_cast<int>(s.waypoints.size());
			for (int i = 0; i < count; ++i) {
				ImGui::PushID(i);
				field(DrawEndpoint(std::format("経由点 {}", i + 1).c_str(), s.waypoints[i], s.is2D, world, entity));
				ImGui::BeginDisabled(i == 0);
				if (ImGui::SmallButton("上へ")) { moveFrom = i; moveTo = i - 1; }
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(i + 1 == count);
				if (ImGui::SmallButton("下へ")) { moveFrom = i; moveTo = i + 1; }
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::SmallButton("削除")) remove = i;
				ImGui::PopID();
			}
			if (0 <= remove) s.waypoints.erase(s.waypoints.begin() + remove);
			if (0 <= moveFrom) std::swap(s.waypoints[moveFrom], s.waypoints[moveTo]);
			const bool add = ImGui::Button("経由点を追加");
			if (add) s.waypoints.emplace_back();
			if (add || 0 <= remove || 0 <= moveFrom) field({true, false, true});
		}
		field(DrawEndpoint("終点", s.end, s.is2D, world, entity));
	}
	if (s.mode == BeamMode::Lightning) {
		field(MyGUI::DragInt("分割数", s.subdivisions, {.minValue = 1, .maxValue = 256}));
		number("振幅", s.amplitude);
		field(MyGUI::DragFloat("更新周波数", s.frequency, {.minValue = 0.0f, .maxValue = 240.0f}));
		if (MyGUI::BeginPropertyRow("乱数シード")) {
			const bool changed = ImGui::InputScalar("##Seed", ImGuiDataType_U32, &s.seed);
			field({changed, ImGui::IsItemActive(), ImGui::IsItemDeactivatedAfterEdit()});
			MyGUI::EndPropertyRow();
		}
		field(MyGUI::DragInt("分岐数", s.branchCount, {.minValue = 0, .maxValue = 64}));
		number("分岐の長さ", s.branchLength);
		number("分岐の幅の倍率", s.branchWidth);
	}
	field(MyGUI::CheckboxField("衝突位置で停止", s.stopOnHit));
	if (s.stopOnHit) {
		field(DrawInspectorMask(context, "衝突レイヤー", s.collisionMask, InspectorMaskDomain::Collision));
		field(MyGUI::EnumCombo("トリガーの扱い", s.triggerInteraction));
	}

	field(MyGUI::CheckboxField(GetInspectorLabel(InspectorFieldID::PlayOnStart), s.playOnStart));
	field(DrawTimeSourceField(context, world, entity, s.timeSource));
	field(MyGUI::EnumCombo("再生設定", s.playbackMode));
	if (s.playbackMode == BeamPlaybackMode::OneShot) number(GetInspectorLabel(InspectorFieldID::Duration), s.duration);
	constexpr InspectorPlaybackAction actions[]{InspectorPlaybackAction::Play, InspectorPlaybackAction::Stop};
	const auto action = DrawInspectorPlaybackControls(true, false, actions);
	if (action == InspectorPlaybackAction::Play) PlayBeam(world, entity);
	if (action == InspectorPlaybackAction::Stop) StopBeam(world, entity);
}
