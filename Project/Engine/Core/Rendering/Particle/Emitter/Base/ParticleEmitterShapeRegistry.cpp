#include "ParticleEmitterShapeRegistry.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>

//============================================================================
//	ParticleEmitterShapeRegistry classMethods
//============================================================================
Engine::ParticleEmitterShapeRegistry& Engine::ParticleEmitterShapeRegistry::GetInstance() {

	static ParticleEmitterShapeRegistry instance;
	return instance;
}

uint32_t Engine::ParticleEmitterShapeRegistry::Register(
	ParticleEmitterShape shape, std::unique_ptr<IParticleEmitterShape> instance) {

	// 既に登録済みならそのまま返す
	auto it = shapes_.find(shape);
	if (it != shapes_.end()) {
		return static_cast<uint32_t>(shapes_.size());
	}

	shapes_[shape] = std::move(instance);
	return static_cast<uint32_t>(shapes_.size());
}

const Engine::IParticleEmitterShape* Engine::ParticleEmitterShapeRegistry::Find(ParticleEmitterShape shape) const {

	auto it = shapes_.find(shape);
	if (it == shapes_.end()) {
		return nullptr;
	}
	return it->second.get();
}

std::vector<Engine::ParticleEmitterShape> Engine::ParticleEmitterShapeRegistry::GetShapes(bool is2D) const {

	std::vector<ParticleEmitterShape> shapes;
	shapes.reserve(shapes_.size());
	for (const auto& [shape, instance] : shapes_) {
		if (is2D ? instance->Supports2D() : instance->Supports3D()) {
			shapes.emplace_back(shape);
		}
	}
	std::sort(shapes.begin(), shapes.end());
	return shapes;
}
