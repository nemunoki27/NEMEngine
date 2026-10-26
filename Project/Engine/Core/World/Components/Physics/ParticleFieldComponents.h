#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleMotionSettings.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

namespace Engine {

	struct ForceFieldComponent {

		ParticleForceFieldType type = ParticleForceFieldType::Attraction;
		Vector3 axis{0, 1, 0};
		float strength = 1;
		float range = 10;
		float falloff = 1;
		uint32_t layerMask = 1;
		bool enabled = true;
	};

	struct VectorFieldComponent {

		AssetID field{};
		Vector3 size{10, 10, 10};
		ParticleVectorFieldPreset preset = ParticleVectorFieldPreset::Wind;
		float intensity = 1;
		float tightness = 0;
		uint32_t layerMask = 1;
		bool enabled = true;
	};

	struct HeightmapColliderComponent {

		AssetID heightmap{};
		Vector3 size{10, 1, 10};
		uint32_t layerMask = 1;
		bool enabled = true;
	};

	void from_json(const nlohmann::json& in, ForceFieldComponent& component);
	void to_json(nlohmann::json& out, const ForceFieldComponent& component);
	void from_json(const nlohmann::json& in, VectorFieldComponent& component);
	void to_json(nlohmann::json& out, const VectorFieldComponent& component);
	void from_json(const nlohmann::json& in, HeightmapColliderComponent& component);
	void to_json(nlohmann::json& out, const HeightmapColliderComponent& component);
}
