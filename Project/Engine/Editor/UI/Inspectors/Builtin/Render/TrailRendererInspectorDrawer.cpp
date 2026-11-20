#include "TrailRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/RibbonRendererFields.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/RendererRenderSettingsFields.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>

using namespace Engine;

void TrailRendererInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const TrailRendererComponent&) {

	if (inspected_ != world.GetUUID(entity)) {
		inspected_ = world.GetUUID(entity);
		invalidMaterial_ = false;
	}
	const auto* record = TryGetTrailRenderer(world, entity);
	settings_ = record ? record->settings : TrailRendererSettings{};
	rootID_ = record && world.IsAlive(record->runtime.rootOverride) ? world.GetUUID(record->runtime.rootOverride) : UUID{};
	tipID_ = record && world.IsAlive(record->runtime.tipOverride) ? world.GetUUID(record->runtime.tipOverride) : UUID{};
}

void TrailRendererInspectorDrawer::SerializeDraft(ECSWorld&, const Entity&, const TrailRendererComponent&, nlohmann::json& out) const {

	out = settings_;
}

void TrailRendererInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity, const TrailRendererComponent&) {

	SetTrailRendererSettings(world, entity, settings_);
	if (auto* record = TryGetTrailRenderer(world, entity)) {
		const Entity root = rootID_ ? world.FindByUUID(rootID_) : Entity::Null();
		const Entity tip = tipID_ ? world.FindByUUID(tipID_) : Entity::Null();
		if (record->runtime.rootOverride != root || record->runtime.tipOverride != tip) {
			record->runtime.rootOverride = root;
			record->runtime.tipOverride = tip;
			BreakTrail(world, entity);
		}
	}
}

void TrailRendererInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) {

	auto& s = settings_;
	const auto field = [&](const ValueEditResult& result) { PushEditResult(result, active); };
	const auto number = [&](const char* label, float& value) {
		field(MyGUI::DragFloat(label, value, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f }));
	};

	field(DrawInspectorCommonFields(context, { .visible = &s.visible, .displayLayer = &s.renderingLayerMask }));
	field(RibbonRendererFields::DrawMaterial(context, s.material, invalidMaterial_, BuiltinAssets::Materials::DefaultTrail));
	field(DrawRendererRenderSettingsFields(s.renderSettings));
	InspectorDrawerCommon::DrawCommonRenderFields(context, [&](auto&& f) { field(f()); }, s.layer, s.order, s.blendMode, s.queue);

	field(MyGUI::CheckboxField("2D描画", s.is2D));
	field(MyGUI::EnumCombo("生成方式", s.mode));
	field(MyGUI::CheckboxField("軌跡を生成", s.emitting));
	field(DrawTimeSourceField(context, world, entity, s.timeSource));
	number("寿命（秒）", s.lifetime);
	number("頂点の最小距離", s.minVertexDistance);
	if (s.mode == TrailMode::TwoPoints) {
		field(SceneEntityReferenceFields::Draw("根元", world, entity, s.root, {}, &rootID_));
		field(SceneEntityReferenceFields::Draw("先端", world, entity, s.tip, {}, &tipID_));
	} else {
		if (!s.is2D) field(MyGUI::EnumCombo("向き", s.alignment));
		number("幅", s.width);
	}
	number("幅の倍率", s.widthMultiplier);
	field(MyGUI::EnumCombo("テクスチャモード", s.textureMode));

	InspectorIndentScope scope;
	RibbonRendererFields::DrawCurve("幅カーブ", s.widthCurve, widthState_, field);
	RibbonRendererFields::DrawCurve("カラーグラデーション", s.colorGradient, colorState_, field);
}
