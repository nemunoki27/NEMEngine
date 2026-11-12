#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>

namespace Engine {

	class AssetDatabase;
	// 内容世代が変わったアセットだけを読み直す
	std::shared_ptr<const TimelineAsset> GetTimelineAsset(ECSWorld& world, AssetDatabase& database,
		AssetID id, std::string& diagnostic);
	// 保存せずに現在のWorldへ編集済み世代を公開する
	bool PublishTimelineAssetOverride(ECSWorld& world, AssetID id,
		std::shared_ptr<const TimelineAsset> asset, std::string* diagnostic = nullptr);
	void ClearTimelineAssetOverride(ECSWorld& world, AssetID id);
	void ClearTimelineAssetOverrides(ECSWorld& world);
	// 保存参照と実行時の参照を同じ規則で解決する
	Entity ResolveTimelineBinding(const ECSWorld& world, Entity owner,
		const TimelinePlayerSettings& settings, UUID slot);
	UUID FindTimelineBinding(const TimelineAsset& asset, std::string_view name);
	UUID FindTimelineParameter(const TimelineAsset& asset, std::string_view name);
	const TimelineParameterValue* GetTimelineParameterValue(const TimelineAsset& asset,
		const TimelinePlayerSettings& settings, UUID parameter);
	void SetTimelineParameterValue(TimelinePlayerSettings& settings, UUID parameter, const TimelineParameterValue& value);
	bool HasTimelineBindingComponent(const ECSWorld& world, Entity entity, std::string_view component);
}
