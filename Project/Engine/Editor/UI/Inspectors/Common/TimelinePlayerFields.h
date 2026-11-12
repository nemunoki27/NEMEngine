#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace Engine {

	struct EditorPanelContext;
	// 同じ割り当てを使う全トラックの必要条件を満たすか確認する
	bool IsTimelineBindingTargetAllowed(const ECSWorld& world, const TimelineAsset& asset, UUID slotID, Entity target);

	// PlayerとToolで同じ割り当て編集を使う
	ValueEditResult DrawTimelineBindingFields(ECSWorld& world, Entity owner,
		TimelinePlayerSettings& settings, const TimelineAsset& asset);
	// 共有する全トラックの必要条件で割り当てを検証する
	ValueEditResult DrawTimelineBindingField(const char* label, ECSWorld& world, Entity owner,
		TimelinePlayerSettings& settings, const TimelineAsset& asset, UUID slotID);
	// Player固有の値だけを上書きする
	ValueEditResult DrawTimelineParameterOverrides(TimelinePlayerSettings& settings, const TimelineAsset& asset);
}
