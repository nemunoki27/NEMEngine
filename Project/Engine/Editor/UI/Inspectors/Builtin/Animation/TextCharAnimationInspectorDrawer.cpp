#include "TextCharAnimationInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Text/TextCharAnimationSampler.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>

using namespace Engine;

void TextCharAnimationInspectorDrawer::CopyEditFields(const TextCharAnimationComponent& source, TextCharAnimationComponent& draft) const {

	static_cast<TextCharAnimationSettings&>(draft) = source;
	draft.runtimeTranslationSourceID = source.runtimeTranslationSourceID;
	draft.runtimeRotationSourceID = source.runtimeRotationSourceID;
	draft.runtimeScaleSourceID = source.runtimeScaleSourceID;
	draft.runtimeColorSourceID = source.runtimeColorSourceID;
}

void TextCharAnimationInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity,
	const TextCharAnimationComponent& component) {

	if (auto* current = world.TryGetComponent<TextCharAnimationComponent>(entity)) {
		CopyEditFields(component, *current);
		world.MarkComponentModified<TextCharAnimationComponent>(entity);
	}
}

void TextCharAnimationInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& active) {

	auto& draft = GetDraft();
	DrawField(active, [&] { return DrawPlaybackStartFields(context, world, entity, draft.timeSource,
		draft.playInEditMode, &draft.playOnStart); });
	constexpr const char* labels[]{"移動", "回転", "スケール", "色"};
	auto* database = context.editorContext->assetDatabase;
	auto* system = context.editorContext->systemContext;
	for (size_t slot = 0; slot < std::size(labels); ++slot) {
		EntityEditSetting options;
		options.acceptEntity = [slot](ECSWorld& source, Entity target) {
			const auto* animation = source.TryGetComponentForBinding<SimpleAnimationComponent>(target);
			return animation && !TextCharAnimationSampler::ValidateSimple(slot, animation->settings);
		};
		options.rejectReason = "この項目に対応するSimpleAnimationを指定してください";
		const auto validateAsset = [&](AssetID asset) -> const char* {
			if (!database || !system || !system->animationClipManager) return "アセットを参照できません";
			return TextCharAnimationSampler::ValidateAsset(slot, world, *database, *system->animationClipManager, asset);
		};
		DrawField(active, [&] {
			auto result = SceneEntityReferenceFields::DrawObjectOrAsset(labels[slot], world, entity, draft.SceneSource(slot),
				draft.RuntimeSource(slot), draft.sourceAssets[slot], database, {AssetType::Prefab, AssetType::AnimationClip},
				options, validateAsset);
			if (result.valueChanged) draft.legacyClip = {};
			return result;
		});
	}
	DrawField(active, [&] { return MyGUI::EnumCombo("再生順", draft.order); });
	if (draft.order != TextCharAnimationOrder::Simultaneous) {
		DrawField(active, [&] { return MyGUI::EnumCombo("複数行の扱い", draft.lines); });
		if (draft.order == TextCharAnimationOrder::Random)
			DrawField(active, [&] { return MyGUI::DragInt("シード", draft.seed); });
		DrawField(active, [&] { return MyGUI::EnumCombo("時間指定", draft.timing); });
		const bool interval = draft.timing == TextCharAnimationTiming::Interval;
		DrawField(active, [&] { return MyGUI::DragFloat(interval ? "文字間隔" : "全体時間",
			interval ? draft.interval : draft.totalDuration, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f}); });
		static const auto easingOptions = [] {
			std::array<ImGuiUtility::EnumOptionState, EnumAdapter<EasingType>::GetEnumCount()> options{};
			for (auto type : magic_enum::enum_values<EasingType>()) {
				options[EnumAdapter<EasingType>::GetIndex(type)].enabled = IsTextCharTimingEasing(type);
			}
			return options;
		}();
		DrawField(active, [&] { return MyGUI::EnumCombo("再生間隔のイージング", draft.easing, {}, easingOptions); });
	}
	DrawField(active, [&] { return MyGUI::EnumCombo(GetInspectorLabel(InspectorFieldID::PlaybackMode), draft.repeat); });
	auto& current = world.GetComponent<TextCharAnimationComponent>(entity);
	const bool playbackEnabled = context.IsPlaying() || draft.playInEditMode;
	static constexpr InspectorPlaybackAction actions[]{InspectorPlaybackAction::Play, InspectorPlaybackAction::Pause,
		InspectorPlaybackAction::Resume, InspectorPlaybackAction::Stop};
	const auto action = DrawInspectorPlaybackControls(playbackEnabled, false, actions);
	if (action != InspectorPlaybackAction::None) current.runtimeRequest = static_cast<int>(action);
	ImGui::BeginDisabled(!playbackEnabled);
	float time = current.runtimeTime;
	if (MyGUI::DragFloat("再生時刻", time, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = draft.repeat == TextCharAnimationRepeat::LoopCharacters ? 1000000.0f : current.runtimeDuration}).valueChanged) {
		current.runtimeSeekTime = time;
		current.runtimeRequest = 6;
	}
	ImGui::EndDisabled();
	MyGUI::TextFloat("一巡の長さ（秒）", current.runtimeDuration);
	MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState), GetInspectorPlaybackState(current.runtimePlaying, current.runtimePaused, current.runtimeFinished));
	if (!current.runtimeDiagnostic.empty()) MyGUI::Text("適用状態", current.runtimeDiagnostic);
}
