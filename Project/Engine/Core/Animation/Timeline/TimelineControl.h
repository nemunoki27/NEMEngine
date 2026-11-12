#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string_view>

namespace Engine {

	class ECSWorld;
	struct TimelineControlOwner {

		UUID player{}, instance{};
		int32_t priority = 0;
		uint64_t order = 0;
	};

	// 制御中のプロパティを通常の更新処理から判別する
	bool IsTimelineControlled(const ECSWorld& world, Entity entity,
		std::string_view component, std::string_view property = {});
	// 優先度と再生開始順で書き込み先を決める
	bool AcquireTimelineControl(ECSWorld& world, Entity entity, std::string_view component,
		std::string_view property, const TimelineControlOwner& owner);
	bool OwnsTimelineControl(const ECSWorld& world, Entity entity, std::string_view component,
		std::string_view property, const TimelineControlOwner& owner);
	// 他の所有者が残る場合は復元値を上書きしない
	bool CanRestoreTimelineControl(const ECSWorld& world, Entity entity, std::string_view component,
		std::string_view property, const TimelineControlOwner& owner);
	void ReleaseTimelineControls(ECSWorld& world, const TimelineControlOwner& owner);
	void ReleaseTimelineControl(ECSWorld& world, Entity entity, std::string_view component,
		std::string_view property, const TimelineControlOwner& owner);
	void ReleaseTimelinePlayerControls(ECSWorld& world, UUID player);
}
