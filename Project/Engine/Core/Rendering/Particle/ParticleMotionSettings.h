#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	enum class ParticleForceFieldType : uint32_t { Attraction, Repulsion, Vortex, Wind };
	enum class ParticleVectorFieldPreset : uint32_t { Wind, Vortex, Asset };
	enum class ParticleCollisionResponse : uint32_t { Bounce, Stop, Kill };

	struct ParticleForceFieldSettings {

		ParticleForceFieldType type = ParticleForceFieldType::Attraction;
		Vector3 center{};
		Vector3 axis{0, 1, 0};
		float strength = 1;
		float range = 10;
		float falloff = 1;
		uint32_t layerMask = UINT32_MAX;
		bool useSceneFields = true;
	};

	struct ParticleVectorFieldSettings {

		float intensity = 1;
		float tightness = 0;
		uint32_t layerMask = UINT32_MAX;
	};

	struct ParticleSplineMovementSettings {

		SceneEntityReference spline{};
		float speed = 1;
		float startDistance = 0;
		float orbitRadius = 0;
		float orbitTurnsPerMeter = 0;
		bool loop = true;
		bool orientToTangent = true;
	};

	struct ParticleCollisionSettings {

		bool enabled = false;
		bool sceneColliders = true;
		bool plane = false;
		bool depth = false;
		bool heightmap = false;
		bool includeTriggers = false;
		Vector3 planeNormal{0, 1, 0};
		float planeDistance = 0;
		float radius = 0.05f;
		float bounce = 0.5f;
		float friction = 0;
		float depthThickness = 0.2f;
		uint32_t layerMask = UINT32_MAX;
		ParticleCollisionResponse response = ParticleCollisionResponse::Bounce;
		SceneEntityReference depthCamera{};
	};

	void from_json(const nlohmann::json& in, ParticleForceFieldSettings& settings);
	void to_json(nlohmann::json& out, const ParticleForceFieldSettings& settings);
	void from_json(const nlohmann::json& in, ParticleVectorFieldSettings& settings);
	void to_json(nlohmann::json& out, const ParticleVectorFieldSettings& settings);
	void from_json(const nlohmann::json& in, ParticleSplineMovementSettings& settings);
	void to_json(nlohmann::json& out, const ParticleSplineMovementSettings& settings);
	void from_json(const nlohmann::json& in, ParticleCollisionSettings& settings);
	void to_json(nlohmann::json& out, const ParticleCollisionSettings& settings);
}
