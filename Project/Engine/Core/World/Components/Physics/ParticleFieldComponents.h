#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Rendering/Particle/ParticleMotionSettings.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

namespace Engine {

	struct ForceFieldComponent {

		ParticleForceFieldType type = ParticleForceFieldType::Attraction;
		Vector3 axis{0, 1, 0};
		float strength = 1;
		float range = 10;
		Vector3 size{20, 20, 20};
		ParticleFieldShape shape = ParticleFieldShape::Sphere;
		bool drawDebug = true;
		float falloff = 1;
		uint32_t layerMask = 1;
		bool enabled = true;
	};

	struct VectorFieldComponent {

		AssetID field{};
		float range = 5;
		ParticleFieldShape shape = ParticleFieldShape::OBB;
		bool drawDebug = true;
		Vector3 size{10, 10, 10};
		ParticleVectorFieldPreset preset = ParticleVectorFieldPreset::Wind;
		float intensity = 1;
		float tightness = 0;
		uint32_t layerMask = 1;
		bool enabled = true;
	};

	struct ParticleFieldBounds {
		Vector3 center{}, halfSize{};
		Quaternion rotation = Quaternion::Identity();
		bool Build(ParticleFieldShape shape, float radius, Vector3 size, const Matrix4x4& world);
		Matrix4x4 UnitToWorld() const;
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
