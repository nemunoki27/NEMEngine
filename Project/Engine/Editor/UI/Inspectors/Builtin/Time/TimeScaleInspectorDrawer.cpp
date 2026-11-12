#include "TimeScaleInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/World/Time/LocalTime.h>

void Engine::TimeScaleInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("時間倍率", draft.timeScale,
		{.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f}); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField("子階層へ適用", draft.applyToChildren); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField("親の時間倍率を継承", draft.inheritParentScale); });
	ImGui::SeparatorText("適用するコンポーネント");
	const char* labels[]{"SkinnedAnimation", "ParticleSystem", "SimpleAnimation", "AnimationClipPlayer",
		"FlipbookAnimation", "MotionAnimation", "Timer", "DayNightCycle", "TimelinePlayer"};
	for (uint32_t index = 0; index < 9; ++index) {
		DrawField(anyItemActive, [&] {
			const uint32_t bit = 1u << index;
			bool enabled = (draft.channels & bit) != 0;
			auto result = InspectorDrawerCommon::DrawCheckboxField(labels[index], enabled);
			if (result.valueChanged) draft.channels = enabled ? draft.channels | bit : draft.channels & ~bit;
			return result;
		});
	}
}
