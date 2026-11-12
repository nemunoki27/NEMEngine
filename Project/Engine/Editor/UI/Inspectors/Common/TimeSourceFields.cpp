#include "TimeSourceFields.h"

//============================================================================
//	include
//============================================================================
#include "InspectorPresentation.h"
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>

Engine::ValueEditResult Engine::DrawPlaybackStartFields(const EditorPanelContext& context, ECSWorld& world,
	Entity owner, TimeSourceSettings& settings, bool& playInEditMode, bool* playOnStart) {

	auto result = DrawTimeSourceField(context, world, owner, settings);
	if (playOnStart) result.Merge(MyGUI::CheckboxField(GetInspectorLabel(InspectorFieldID::PlayOnStart), *playOnStart));
	result.Merge(MyGUI::CheckboxField(GetInspectorLabel(InspectorFieldID::PlayInEditMode), playInEditMode));
	return result;
}

Engine::ValueEditResult Engine::DrawTimeSourceField([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, Entity owner, TimeSourceSettings& settings) {

	ValueEditResult result;
	if (MyGUI::BeginPropertyRow("時間源")) {
		const char* labels[]{"自身・親階層", "指定したTimeScale", "グローバル時間のみ", "非スケール時間"};
		int mode = static_cast<int>(settings.mode);
		result.valueChanged = ImGui::Combo("##TimeSource", &mode, labels, 4);
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged;
		if (result.valueChanged) settings.mode = static_cast<TimeSourceMode>(mode);
		MyGUI::EndPropertyRow();
	}
	if (settings.mode == TimeSourceMode::Explicit) {
		EntityEditSetting referenceSetting;
		referenceSetting.acceptEntity = [](ECSWorld& source, Entity target) {
			return source.TryGetComponentForBinding<TimeScaleComponent>(target) != nullptr;
		};
		referenceSetting.rejectReason = "TimeScaleコンポーネントが必要です";
		auto referenceEdit = SceneEntityReferenceFields::Draw("時間源オブジェクト", world, owner, settings.source,
			referenceSetting, &settings.runtimeSourceID);
		result.Merge(referenceEdit);
		const Entity source = ResolveTimeSourceEntity(world, owner, settings);
		if (!world.TryGetComponentForBinding<TimeScaleComponent>(source))
			MyGUI::Text("時間源の状態", "TimeScaleが未設定のため時間を停止しています");
	}
	return result;
}
