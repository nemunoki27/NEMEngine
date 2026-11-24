#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/World/Components/Physics/ParticleFieldComponents.h>

// c++
#include <memory>
#include <vector>

namespace Engine {
	class ECSWorld;
	class AssetDatabase;
	class ParticleFieldAssetCache;
	struct VectorFieldData;
}

namespace Engine::ClothWind {

	struct WindField {
		Matrix4x4 inverse, world;
		Vector3 center{}, axis{};
		uint32_t mask = 0;
		ParticleFieldShape shape{};
		ParticleForceFieldType forceType{};
		ParticleVectorFieldPreset preset{};
		float strength = 0, falloff = 0, tightness = 0;
		bool vector = false;
		std::shared_ptr<const VectorFieldData> grid;
	};

	std::vector<WindField> CollectWind(ECSWorld& world, const AssetDatabase* database,
		std::unique_ptr<ParticleFieldAssetCache>& assets);
	Vector3 SampleForce(const WindField& field, Vector3 position, Vector3 velocity, float deltaTime);
}
