#include "ParticleParametricShapeRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Parametric/ParticleCylinderParametricShape.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleRingParametricShape.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleParametricShapeRegistry classMethods
//============================================================================
Engine::ParticleParametricShapeRegistry::ParticleParametricShapeRegistry() {

	Register(PrimitiveType::Ring, std::make_unique<ParticleRingParametricShape>());
	Register(PrimitiveType::Cylinder, std::make_unique<ParticleCylinderParametricShape>());
}

Engine::ParticleParametricShapeRegistry& Engine::ParticleParametricShapeRegistry::GetInstance() {

	static ParticleParametricShapeRegistry instance;
	return instance;
}

uint32_t Engine::ParticleParametricShapeRegistry::Register(
	PrimitiveType type, std::unique_ptr<IParticleParametricShape> instance) {

	// 既に登録済みならそのまま返す
	auto it = shapes_.find(type);
	if (it != shapes_.end()) {
		return static_cast<uint32_t>(shapes_.size());
	}

	shapes_[type] = std::move(instance);
	return static_cast<uint32_t>(shapes_.size());
}

const Engine::IParticleParametricShape* Engine::ParticleParametricShapeRegistry::Find(PrimitiveType type) const {

	auto it = shapes_.find(type);
	if (it == shapes_.end()) {
		return nullptr;
	}
	return it->second.get();
}

std::vector<Engine::PrimitiveType> Engine::ParticleParametricShapeRegistry::GetTypes() const {

	std::vector<PrimitiveType> types;
	types.reserve(shapes_.size());
	for (const auto& [type, instance] : shapes_) {
		types.emplace_back(type);
	}
	std::sort(types.begin(), types.end());
	return types;
}
