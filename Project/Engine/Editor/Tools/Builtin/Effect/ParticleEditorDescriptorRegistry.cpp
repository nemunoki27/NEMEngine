#include "ParticleEditorDescriptorRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleAlphaReferenceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleEmissiveModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleFlipbookModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleGravityForceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleLookToVelocityModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseForceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticlePendulumMovementModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleRotationModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleScaleOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleShapeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSpiralMovementModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailColorUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleBoxEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCircleEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCone2DEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleConeEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleHemisphereEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticlePointEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleRectEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleSphereEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleTorusEmitterShape.h>

//============================================================================
//	ParticleEditorDescriptorRegistry classMethods
//============================================================================
namespace {

	template<typename T>
	bool DrawParticleModule(Engine::IParticleModule& module) {

		auto* concrete = dynamic_cast<T*>(&module);
		return concrete ? concrete->DrawImGui() : false;
	}

	template<typename T>
	bool DrawParticleEmitterShape(const Engine::IParticleEmitterShape& emitterShape,
		Engine::ParticleEmitterSettings& settings) {

		const auto* concrete = dynamic_cast<const T*>(&emitterShape);
		return concrete ? concrete->DrawImGui(settings) : false;
	}
}

Engine::ParticleEditorDescriptorRegistry::ParticleEditorDescriptorRegistry() {

	ParticleModuleRegistry& registry = ParticleModuleRegistry::GetInstance();
	moduleDrawers_.resize(registry.GetDescriptors().size(), nullptr);
	auto registerModule = [&](const char* id, ModuleDrawFunc draw) {

		const ParticleModuleRegistry::TypeID typeID = registry.FindTypeID(id);
		if (typeID != ParticleModuleRegistry::kInvalidTypeID && typeID < moduleDrawers_.size()) {
			moduleDrawers_[typeID] = draw;
		}
		};

	registerModule("AlphaReference", &DrawParticleModule<ParticleAlphaReferenceModule>);
	registerModule("ColorOverLifetime", &DrawParticleModule<ParticleColorOverLifetimeModule>);
	registerModule("ColorUV", &DrawParticleModule<ParticleColorUVModule>);
	registerModule("CustomShaderParameter", &DrawParticleModule<ParticleCustomShaderParameterModule>);
	registerModule("Emissive", &DrawParticleModule<ParticleEmissiveModule>);
	registerModule("Flipbook", &DrawParticleModule<ParticleFlipbookModule>);
	registerModule("GravityForce", &DrawParticleModule<ParticleGravityForceModule>);
	registerModule("LookToVelocity", &DrawParticleModule<ParticleLookToVelocityModule>);
	registerModule("NoiseForce", &DrawParticleModule<ParticleNoiseForceModule>);
	registerModule("NoiseUV", &DrawParticleModule<ParticleNoiseUVModule>);
	registerModule("PendulumMovement", &DrawParticleModule<ParticlePendulumMovementModule>);
	registerModule("Rotation", &DrawParticleModule<ParticleRotationModule>);
	registerModule("ScaleOverLifetime", &DrawParticleModule<ParticleScaleOverLifetimeModule>);
	registerModule("ShapeOverLifetime", &DrawParticleModule<ParticleShapeOverLifetimeModule>);
	registerModule("SizeOverLifetime", &DrawParticleModule<ParticleSizeOverLifetimeModule>);
	registerModule("SpiralMovement", &DrawParticleModule<ParticleSpiralMovementModule>);
	registerModule("TrailColorOverLifetime", &DrawParticleModule<ParticleTrailColorOverLifetimeModule>);
	registerModule("TrailColorUV", &DrawParticleModule<ParticleTrailColorUVModule>);
	registerModule("TrailCustomShaderParameter", &DrawParticleModule<ParticleTrailCustomShaderParameterModule>);
	registerModule("TrailSizeOverLifetime", &DrawParticleModule<ParticleTrailSizeOverLifetimeModule>);

	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Sphere)] =
		&DrawParticleEmitterShape<ParticleSphereEmitterShape>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Hemisphere)] =
		&DrawParticleEmitterShape<ParticleHemisphereEmitterShape>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Box)] =
		&DrawParticleEmitterShape<ParticleBoxEmitterShape>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Torus)] =
		&DrawParticleEmitterShape<ParticleTorusEmitterShape>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Circle)] =
		&DrawParticleEmitterShape<ParticleCircleEmitterShape>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Cone)] =
		&DrawParticleEmitterShape<ParticleConeEmitterShape>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Point)] =
		&DrawParticleEmitterShape<ParticlePointEmitterShape>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Rect)] =
		&DrawParticleEmitterShape<ParticleRectEmitterShape>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Cone2D)] =
		&DrawParticleEmitterShape<ParticleCone2DEmitterShape>;
}

Engine::ParticleEditorDescriptorRegistry& Engine::ParticleEditorDescriptorRegistry::GetInstance() {

	static ParticleEditorDescriptorRegistry instance;
	return instance;
}

bool Engine::ParticleEditorDescriptorRegistry::DrawModule(
	ParticleModuleRegistry::TypeID typeID, IParticleModule& module) const {

	if (typeID == ParticleModuleRegistry::kInvalidTypeID || typeID >= moduleDrawers_.size()) {
		return false;
	}
	const ModuleDrawFunc draw = moduleDrawers_[typeID];
	return draw ? draw(module) : false;
}

bool Engine::ParticleEditorDescriptorRegistry::DrawEmitterShape(ParticleEmitterShape shape,
	const IParticleEmitterShape& emitterShape, ParticleEmitterSettings& settings) const {

	const size_t index = static_cast<size_t>(shape);
	if (index >= emitterDrawers_.size()) {
		return false;
	}
	const EmitterDrawFunc draw = emitterDrawers_[index];
	return draw ? draw(emitterShape, settings) : false;
}
