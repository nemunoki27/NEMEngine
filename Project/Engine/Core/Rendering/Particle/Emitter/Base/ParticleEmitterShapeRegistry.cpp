#include "ParticleEmitterShapeRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleBoxEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCircleEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCone2DEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleConeEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleHemisphereEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticlePointEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleRectEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleSphereEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleTorusEmitterShape.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleEmitterShapeRegistry classMethods
//============================================================================
Engine::ParticleEmitterShapeRegistry::ParticleEmitterShapeRegistry() {

	Register(ParticleEmitterShape::Sphere, std::make_unique<ParticleSphereEmitterShape>());
	Register(ParticleEmitterShape::Hemisphere, std::make_unique<ParticleHemisphereEmitterShape>());
	Register(ParticleEmitterShape::Box, std::make_unique<ParticleBoxEmitterShape>());
	Register(ParticleEmitterShape::Torus, std::make_unique<ParticleTorusEmitterShape>());
	Register(ParticleEmitterShape::Circle, std::make_unique<ParticleCircleEmitterShape>());
	Register(ParticleEmitterShape::Cone, std::make_unique<ParticleConeEmitterShape>());
	Register(ParticleEmitterShape::Point, std::make_unique<ParticlePointEmitterShape>());
	Register(ParticleEmitterShape::Rect, std::make_unique<ParticleRectEmitterShape>());
	Register(ParticleEmitterShape::Cone2D, std::make_unique<ParticleCone2DEmitterShape>());
}

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
