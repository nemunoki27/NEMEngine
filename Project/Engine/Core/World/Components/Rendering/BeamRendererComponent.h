#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/RendererRenderSettings.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/Time/LocalTime.h>

#include <Engine/Core/Physics/Collision/CollisionQuery.h>
#include "TrailRendererComponent.h"

namespace Engine {

	enum class BeamMode : uint8_t { Beam, Lightning };
	enum class BeamEndpointMode : uint8_t { Local, World, Object };
	enum class BeamPlaybackMode : uint8_t { Continuous, OneShot };

	struct BeamEndpoint {
		BeamEndpointMode mode = BeamEndpointMode::Local;
		Vector3 position{};
		SceneEntityReference object{};
		Entity runtimeObject = Entity::Null();
	};

	struct BeamRendererSettings {
		BeamRendererSettings();
		BeamMode mode = BeamMode::Beam;
		BeamEndpoint start{}, end{BeamEndpointMode::Local, {0, 0, 3}};
		std::vector<BeamEndpoint> waypoints;
		bool is2D = false;
		bool useWidthCurve = false;
		float width = 0.1f, widthMultiplier = 1;
		CurveFloat widthCurve;
		int32_t subdivisions = 16, branchCount = 3;
		float amplitude = 0.15f, frequency = 12, branchLength = 0.5f, branchWidth = 0.4f;
		uint32_t seed = 1;
		BeamPlaybackMode playbackMode = BeamPlaybackMode::Continuous;
		bool playOnStart = true, stopOnHit = false;
		float duration = 0.5f;
		uint32_t collisionMask = ~0u;
		QueryTriggerInteraction triggerInteraction = QueryTriggerInteraction::Ignore;
		TimeSourceSettings timeSource{};
		AssetID material{};
		MaterialInstanceParameters materialInstance{};
		int32_t layer = 0, order = 0;
		bool visible = true;
		RendererRenderSettings renderSettings{.cullMode = RendererCullMode::NONE, .depthWrite = false};
		uint32_t renderingLayerMask = 1u;
		BlendMode blendMode = BlendMode::Normal;
		RenderPhase queue = RenderPhase::Transparent;
	};

	struct BeamPoint {
		Vector3 position{};
		float progress = 0, distance = 0, widthScale = 1;
		bool startStrip = false;
	};

	struct BeamRendererRuntime {
		bool initialized = false, playing = false, timelineControlled = false, hasHit = false;
		double time = 0;
		RaycastHit3D hit{};
		int32_t hitSegment = -1;
		std::vector<BeamPoint> points;
		std::vector<Vector3> anchors;
		std::vector<Entity> excluded;
	};
	struct BeamRendererRecord {
		BeamRendererSettings settings;
		BeamRendererRuntime runtime;
		UUID owner{};
	};
	struct BeamRendererStorageTag;
	using BeamRendererStorage = GenerationalPool<BeamRendererRecord, BeamRendererStorageTag>;

	struct BeamRendererComponent {
		static constexpr bool kHasECSHooks = true;
		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels = ComponentChangeChannel::Render;
		BeamRendererStorage::Handle handle{};
		static void OnAdded(ECSWorld&, const Entity&, BeamRendererComponent&);
		static void OnRemoved(ECSWorld&, const Entity&);
		static void InitializeStorage(ECSWorld&, const Entity&, BeamRendererComponent&);
		static void ReleaseStorage(ECSWorld&, const Entity&, BeamRendererComponent&);
		static void CloneStorage(const ECSWorld&, ECSWorld&, const Entity&, const BeamRendererComponent&, BeamRendererComponent&);
		static void DeserializeECS(ECSWorld&, const Entity&, const nlohmann::json&, BeamRendererComponent&);
		static void SerializeECS(const ECSWorld&, const Entity&, const BeamRendererComponent&, nlohmann::json&);
	};

	void from_json(const nlohmann::json& in, BeamRendererSettings& settings);
	void to_json(nlohmann::json& out, const BeamRendererSettings& settings);
	BeamRendererRecord* TryGetBeamRenderer(ECSWorld& world, Entity owner);
	const BeamRendererRecord* TryGetBeamRenderer(const ECSWorld& world, Entity owner);
	void SetBeamRendererSettings(ECSWorld& world, Entity owner, const BeamRendererSettings& settings);
	void PlayBeam(ECSWorld& world, Entity owner);
	void StopBeam(ECSWorld& world, Entity owner);
	void UpdateBeamGeometry(ECSWorld& world, Entity owner, BeamRendererRecord& record);
	void from_json(const nlohmann::json& in, BeamEndpoint& point);
	void to_json(nlohmann::json& out, const BeamEndpoint& point);
}
