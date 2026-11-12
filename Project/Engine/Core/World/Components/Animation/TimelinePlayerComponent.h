#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineAsset.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/Time/LocalTime.h>

// c++
#include <memory>
#include <unordered_set>

namespace Engine {

	class ECSWorld;
	struct TimelinePlaybackState;
	struct TimelinePlayerBinding {

		UUID slot{};
		SceneEntityReference target{};
		UUID runtimeTargetID{};
	};
	struct TimelineParameterOverride {

		UUID parameter{};
		TimelineParameterValue value = 0.0f;
	};
	struct TimelinePlayerSettings {

		bool enabled = true;
		bool playOnStart = false;
		bool playInEditMode = false;
		AssetID timeline{};
		TimeSourceSettings timeSource{};
		float speed = 1.0f;
		double startDelay = 0.0;
		bool loop = false;
		int32_t priority = 0;
		std::vector<TimelinePlayerBinding> bindings;
		std::vector<TimelineParameterOverride> parameters;
	};
	struct TimelineNotification {

		std::string name;
		double time = 0.0;
		float floatParam = 0.0f;
		int32_t intParam = 0;
		std::string stringParam;
		Entity target = Entity::Null();
	};
	struct TimelinePlayerRuntime {

		bool initialized = false;
		bool playing = false;
		bool paused = false;
		bool finished = false;
		bool edit = false;
		bool preparing = false;
		float preparationProgress = 1.0f;
		int32_t request = 0;
		double seekTime = 0.0;
		double time = 0.0;
		double duration = 0.0;
		uint64_t cycles = 0;
		std::string diagnostic;
		std::vector<TimelineNotification> notifications;
		std::vector<TimelineNotification> pendingNotifications;
		std::unordered_set<std::string> windows;
		std::shared_ptr<TimelinePlaybackState> playback;
	};
	struct TimelinePlayerRecord {

		TimelinePlayerSettings settings;
		TimelinePlayerRuntime runtime;
		UUID owner{};
		uint64_t inheritedOrder = 0;
	};
	struct TimelinePlayerStorageTag;
	using TimelinePlayerStorage = GenerationalPool<TimelinePlayerRecord, TimelinePlayerStorageTag>;

	// 再生データはチャンク外で保持する
	struct TimelinePlayerComponent {

		static constexpr bool kHasECSHooks = true;
		TimelinePlayerStorage::Handle handle{};
		static void OnAdded(ECSWorld& world, const Entity& entity, TimelinePlayerComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, TimelinePlayerComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, TimelinePlayerComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity, const nlohmann::json& in,
			TimelinePlayerComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const TimelinePlayerComponent& component, nlohmann::json& out);
		static void CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld, const Entity& entity,
			const TimelinePlayerComponent& source, TimelinePlayerComponent& destination);
	};
	static_assert(sizeof(TimelinePlayerComponent) == 8);

	TimelinePlayerRecord* TryGetTimelinePlayer(ECSWorld& world, Entity entity);
	const TimelinePlayerRecord* TryGetTimelinePlayer(const ECSWorld& world, Entity entity);
	void SetTimelinePlayerSettings(ECSWorld& world, Entity entity, const TimelinePlayerSettings& settings);
	void from_json(const nlohmann::json& in, TimelinePlayerSettings& settings);
	void to_json(nlohmann::json& out, const TimelinePlayerSettings& settings);
}
