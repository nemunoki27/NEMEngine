#include "SimpleAnimationInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SimpleAnimationFields.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>

// c++
#include <map>

void Engine::SimpleAnimationInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity,
	const SimpleAnimationComponent& component) {

	if (auto* current = world.TryGetComponent<SimpleAnimationComponent>(entity)) {
		ApplySimpleAnimationSettings(component, *current);
		world.MarkComponentModified<SimpleAnimationComponent>(entity);
	}
}

void Engine::SimpleAnimationInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&] {
		if (draft.settings.unscaledTime) draft.timeSource.mode = TimeSourceMode::Unscaled;
		auto result = DrawPlaybackStartFields(context, world, entity, draft.timeSource, draft.playInEditMode, &draft.playOnStart);
		if (result.valueChanged) draft.settings.unscaledTime = false;
		return result;
	});
	DrawField(anyItemActive, [&] { return SimpleAnimationFields::DrawSettings(draft.settings, true, false); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField("自動適用", draft.autoApply); });
	if (draft.autoApply) {
		Entity target = draft.target.localFileID ? draft.target.Resolve(world, entity) : entity;
		UUID targetID = draft.target.localFileID && world.IsAlive(target) ? world.GetUUID(target) : UUID{};
		DrawField(anyItemActive, [&] {
			EntityEditSetting setting;
			const std::string missing = ToString(draft.target.localFileID);
			setting.hasStoredReference = static_cast<bool>(draft.target.localFileID);
			setting.missingReferenceID = missing;
			auto edit = MyGUI::EntityReferenceField("適用先・未設定は自分", targetID, &world, setting);
			if (edit.valueChanged) draft.target = targetID ? SceneEntityReference::FromEntity(world, world.FindByUUID(targetID)) : SceneEntityReference{};
			return edit;
		});
		target = draft.target.localFileID ? draft.target.Resolve(world, entity) : entity;
		if (world.IsAlive(target)) {
			AnimationPropertyQueryContext query;
			if (context.editorContext) query.assetDatabase = context.editorContext->assetDatabase;
			query.renderPipeline = context.renderPipeline;
			const auto properties = AnimationPropertyRegistry::GetInstance().CollectProperties(world, target, query);
			DrawField(anyItemActive, [&] {
				ValueEditResult edit;
				if (!MyGUI::BeginPropertyRow("適用する項目")) return edit;
				const std::string label = draft.binding.componentName + "." + draft.binding.propertyPath;
				if (ImGui::BeginCombo("##Binding", draft.binding.componentName.empty() ? "未設定" : label.c_str())) {
					std::map<std::string, std::vector<const AnimationPropertyDescriptor*>> groups;
					for (const auto& property : properties) groups[property.componentName].push_back(&property);
					for (const auto& [component, entries] : groups) {
						if (!ImGui::TreeNode(component.c_str())) continue;
						for (const auto* entry : entries) {
							const auto& property = *entry;
							const std::string key = property.componentName + "." + property.propertyPath;
							if (ImGui::Selectable(key.c_str(), key == label)) {
								const size_t type = static_cast<size_t>(property.valueType);
								if (draft.settings.start.index() != type) {
									const auto defaults = MakeSimpleAnimationSettings(type);
									draft.settings.start = defaults.start;
									draft.settings.end = defaults.end;
									draft.settings.center = defaults.center;
									draft.settings.amplitude = defaults.amplitude;
									draft.settings.angularAmplitude = defaults.angularAmplitude;
									draft.settings.rotationMode = defaults.rotationMode;
									draft.settings.startEuler = defaults.startEuler;
									draft.settings.endEuler = defaults.endEuler;
									draft.settings.centerEuler = defaults.centerEuler;
								}
								draft.binding = {property.componentName, property.propertyPath, property.valueType};
								edit.valueChanged = edit.editFinished = true;
							}
						}
						ImGui::TreePop();
					}
					ImGui::EndCombo();
				}
				MyGUI::EndPropertyRow();
				return edit;
			});
		}
	}
	auto& current = world.GetComponent<SimpleAnimationComponent>(entity);
	const bool playbackEnabled = context.IsPlaying() || draft.playInEditMode;
	const auto action = DrawInspectorPlaybackControls(playbackEnabled, false);
	if (action != InspectorPlaybackAction::None) current.runtimeRequest = static_cast<int>(action);
	ImGui::BeginDisabled(!playbackEnabled);
	float time = current.runtimeTime;
	if (MyGUI::DragFloat("再生時刻", time, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f}).valueChanged) {
		current.runtimeSeekTime = time; current.runtimeRequest = 6;
	}
	ImGui::EndDisabled();
	MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState), GetInspectorPlaybackState(current.runtimePlaying, current.runtimePaused, current.runtimeFinished));
	if (current.runtimeValid) {
		SimpleAnimationFields::DrawCurrentValue("現在値", current.runtime.sample.value);
	}
	if (!current.runtimeDiagnostic.empty()) MyGUI::Text("適用状態", current.runtimeDiagnostic);
}
