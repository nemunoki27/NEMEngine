#include "TimelinePlayerFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/Tools/Builtin/Timeline/TimelineEditorFields.h>

// c++
#include <algorithm>

using namespace Engine;

namespace {

	// 共有スロットを使用するすべてのトラックを検証する
	std::vector<std::string> GetRequiredComponents(const TimelineAsset& asset, const TimelineBindingDefinition& slot) {

		std::vector<std::string> required;
		if (!slot.component.empty()) required.push_back(slot.component);
		for (const auto& track : asset.tracks) {
			if (track.binding == slot.id) {
				const auto component = GetTimelineTrackRequiredComponent(track.kind);
				const bool global = track.kind == TimelineTrackKind::TimeScale && std::all_of(track.clips.begin(), track.clips.end(),
					[](const auto& clip) {
						const auto value = clip.data.find("global");
						return value != clip.data.end() && value->is_boolean() && value->template get<bool>();
					});
				if (!global && !component.empty() && std::find(required.begin(), required.end(), component) == required.end())
					required.emplace_back(component);
			}
			if (track.kind == TimelineTrackKind::Motion) for (const auto& clip : track.clips) {
				const auto matches = [&](const char* key) {
					const auto value = clip.data.find(key);
					return value != clip.data.end() && value->is_string() &&
						(value->template get<std::string>() == slot.name || value->template get<std::string>() == ToString(slot.id));
				};
				if (matches("splineBinding")) required.emplace_back("Spline");
				if (matches("targetBinding")) required.emplace_back("Transform");
			}
		}
		return required;
	}
}

bool Engine::IsTimelineBindingTargetAllowed(const ECSWorld& world, const TimelineAsset& asset, UUID slotID, Entity target) {

	if (!HasTimelineBindingComponent(world, target, {})) return false;
	const auto slot = std::find_if(asset.bindings.begin(), asset.bindings.end(),
		[&](const auto& definition) { return definition.id == slotID; });
	if (slot == asset.bindings.end()) return false;
	const auto required = GetRequiredComponents(asset, *slot);
	return std::all_of(required.begin(), required.end(),
		[&](const auto& component) { return HasTimelineBindingComponent(world, target, component); });
}

ValueEditResult Engine::DrawTimelineBindingFields(ECSWorld& world, Entity owner,
	TimelinePlayerSettings& settings, const TimelineAsset& asset) {

	ValueEditResult result;
	for (const auto& slot : asset.bindings) {
		const std::string label(TimelineEditorFields::BindingLabel(slot.name));
		result.Merge(DrawTimelineBindingField(label.c_str(), world, owner, settings, asset, slot.id));
	}
	return result;
}

ValueEditResult Engine::DrawTimelineBindingField(const char* label, ECSWorld& world, Entity owner,
	TimelinePlayerSettings& settings, const TimelineAsset& asset, UUID slotID) {

	const auto definition = std::find_if(asset.bindings.begin(), asset.bindings.end(),
		[&](const auto& slot) { return slot.id == slotID; });
	if (definition == asset.bindings.end()) return {};
	const auto& slot = *definition;
	ImGui::PushID(ToString(slot.id).c_str());
	const auto found = std::find_if(settings.bindings.begin(), settings.bindings.end(),
		[&](const auto& value) { return value.slot == slot.id; });
	TimelinePlayerBinding candidate = found == settings.bindings.end() ? TimelinePlayerBinding{slot.id} : *found;
	const auto required = GetRequiredComponents(asset, slot);
	EntityEditSetting setting;
	setting.acceptEntity = [required](ECSWorld& source, Entity target) {
		return HasTimelineBindingComponent(source, target, {}) && std::all_of(required.begin(), required.end(),
			[&](const auto& component) { return HasTimelineBindingComponent(source, target, component); });
	};
	setting.rejectReason = "トラックに必要なコンポーネントを持つエンティティを設定してください";
	const auto edit = SceneEntityReferenceFields::Draw(label, world, owner,
		candidate.target, setting, &candidate.runtimeTargetID);
	if (edit.valueChanged) {
		if (found == settings.bindings.end()) settings.bindings.push_back(std::move(candidate));
		else *found = std::move(candidate);
	}
	ImGui::PopID();
	return edit;
}

ValueEditResult Engine::DrawTimelineParameterOverrides(TimelinePlayerSettings& settings, const TimelineAsset& asset) {

	ValueEditResult result;
	for (const auto& parameter : asset.parameters) {
		ImGui::PushID(ToString(parameter.id).c_str());
		auto found = std::find_if(settings.parameters.begin(), settings.parameters.end(),
			[&](const auto& value) { return value.parameter == parameter.id; });
		bool enabled = found != settings.parameters.end();
		const auto toggle = MyGUI::CheckboxField(parameter.name.c_str(), enabled);
		result.Merge(toggle);
		if (toggle.valueChanged) {
			if (enabled) {
				settings.parameters.push_back({parameter.id, parameter.value});
				found = std::prev(settings.parameters.end());
			} else settings.parameters.erase(found);
		}
		if (enabled) {
			ImGui::Indent();
			result.Merge(TimelineEditorFields::ParameterValue("上書き値", found->value));
			ImGui::Unindent();
		}
		ImGui::PopID();
	}
	return result;
}
