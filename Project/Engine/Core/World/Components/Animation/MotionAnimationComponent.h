#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/SplineComponent.h>
#include <Engine/Core/World/Time/LocalTime.h>
#include <Engine/Core/Animation/Simple/SimpleAnimationEvaluation.h>

namespace Engine {

	enum class MotionAnimationMode : uint8_t { Relative, ApproachTarget, Segments, Spline };
	enum class MotionAnimationPlane : uint8_t { Full3D, GroundXZ, PlaneXY };
	enum class MotionAnimationRotation : uint8_t { Relative, LookAtTarget, PathTangent };

	struct MotionAnimationSegment {
		Vector3 offset{0, 0, 1};
		float duration = 1;
		EasingType easing = EasingType::Linear;
	};

	struct MotionAnimationSettings {
		bool enabled = true;
		bool playOnStart = true;
		bool playInEditMode = false;
		bool autoApply = true;
		bool applyPosition = true;
		bool applyRotation = false;
		bool trackTarget = false;
		bool drawDebug = true;
		SceneEntityReference target{};
		SceneEntityReference spline{};
		TimeSourceSettings timeSource{};
		MotionAnimationMode mode = MotionAnimationMode::Relative;
		MotionAnimationPlane plane = MotionAnimationPlane::Full3D;
		MotionAnimationRotation rotation = MotionAnimationRotation::Relative;
		SimpleAnimationRotationMode rotationEdit = SimpleAnimationRotationMode::Euler;
		SimpleAnimationRepeat repeat = SimpleAnimationRepeat::Once;
		Vector3 offset{0, 0, 1};
		Vector3 euler{};
		Vector3 axis{0, 1, 0};
		float angle = 0;
		float stoppingDistance = 1;
		float splineStart = 0;
		float splineEnd = 1;
		float duration = 1;
		float delay = 0;
		float speed = 1;
		uint32_t cycleCount = 0;
		EasingType easing = EasingType::Linear;
		std::vector<MotionAnimationSegment> segments{{}};
	};

	struct MotionAnimationRuntime {
		bool initialized = false, playing = false, paused = false, finished = false;
		bool edit = false, frozenTarget = false;
		bool appliedPosition = false, appliedRotation = false;
		bool resourcesDirty = false;
		double time = 0;
		Vector3 origin{}, targetPosition{}, position{}, deltaPosition{};
		Quaternion originRotation{}, rotation{}, deltaRotation{};
		Vector3 originalLocalPosition{};
		Quaternion originalLocalRotation{};
		Entity targetOverride = Entity::Null();
		Entity splineOverride = Entity::Null();
		bool hasSplineOverride = false;
		bool hasTargetOverride = false;
		int32_t request = 0;
		double seekTime = 0;
		uint64_t cycles = 0, pendingCycles = 0, pendingCompletions = 0;
		std::vector<uint64_t> pendingSegments;
		std::vector<SplineSample> splineSamples;
		std::string diagnostic;
	};

	struct MotionAnimationRecord {
		MotionAnimationSettings settings;
		MotionAnimationRuntime runtime;
		UUID owner{};
	};
	struct MotionAnimationStorageTag;
	using MotionAnimationStorage = GenerationalPool<MotionAnimationRecord, MotionAnimationStorageTag>;

	struct MotionAnimationComponent {
		static constexpr bool kHasECSHooks = true;
		MotionAnimationStorage::Handle handle{};
		static void OnAdded(ECSWorld& world, const Entity& entity, MotionAnimationComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, MotionAnimationComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, MotionAnimationComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity, const nlohmann::json& in, MotionAnimationComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity, const MotionAnimationComponent& component, nlohmann::json& out);
		static void CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld, const Entity& entity,
			const MotionAnimationComponent& source, MotionAnimationComponent& destination);
	};
	static_assert(sizeof(MotionAnimationComponent) == 8);

	MotionAnimationRecord* TryGetMotionAnimation(ECSWorld& world, Entity entity);
	const MotionAnimationRecord* TryGetMotionAnimation(const ECSWorld& world, Entity entity);
	void SetMotionAnimationSettings(ECSWorld& world, Entity entity, const MotionAnimationSettings& settings);
	void from_json(const nlohmann::json& in, MotionAnimationSettings& settings);
	void to_json(nlohmann::json& out, const MotionAnimationSettings& settings);
}
