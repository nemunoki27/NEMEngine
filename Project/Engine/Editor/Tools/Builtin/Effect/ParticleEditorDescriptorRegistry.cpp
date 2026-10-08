#include "ParticleEditorDescriptorRegistry.h"
#include "ParticleEmitterShapeDrawers.h"
#include "Modules/ParticleShapeOverLifetimeModuleDrawer.h"
#include "Modules/ParticleCustomShaderParameterModuleDrawer.h"
#include "Modules/ParticlePendulumMovementModuleDrawer.h"
#include "Modules/ParticleSpiralMovementModuleDrawer.h"
#include "Modules/ParticleColorUVModuleDrawer.h"
#include "Modules/ParticleRotationModuleDrawer.h"

//============================================================================
//	include
//============================================================================
#include "Modules/ParticleAlphaReferenceModuleDrawer.h"
#include "Modules/ParticleColorOverLifetimeModuleDrawer.h"
#include "Modules/ParticleEmissiveModuleDrawer.h"
#include "Modules/ParticleFlipbookModuleDrawer.h"
#include "Modules/ParticleGravityForceModuleDrawer.h"
#include "Modules/ParticleNoiseForceModuleDrawer.h"
#include "Modules/ParticleNoiseUVModuleDrawer.h"
#include "Modules/ParticleScaleOverLifetimeModuleDrawer.h"
#include "Modules/ParticleSizeOverLifetimeModuleDrawer.h"
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

	template<typename T, bool(*Draw)(Engine::ParticleEmitterSettings&)>
	bool DrawParticleEmitterShape(const Engine::IParticleEmitterShape& emitterShape,
		Engine::ParticleEmitterSettings& settings) {

		const auto* concrete = dynamic_cast<const T*>(&emitterShape);
		return concrete ? Draw(settings) : false;
	}
}

Engine::ParticleEditorDescriptorRegistry::ParticleEditorDescriptorRegistry() {

	ParticleModuleRegistry& registry = ParticleModuleRegistry::GetInstance();
	moduleDrawerFactories_.resize(registry.GetDescriptors().size(), nullptr);
	auto registerModule = [&](const char* id, ModuleDrawerFactory draw) {

		const ParticleModuleRegistry::TypeID typeID = registry.FindTypeID(id);
		if (typeID != ParticleModuleRegistry::kInvalidTypeID && typeID < moduleDrawerFactories_.size()) {
			moduleDrawerFactories_[typeID] = draw;
		}
		};

	registerModule("AlphaReference", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleAlphaReferenceModuleDrawer>();
	});
	registerModule("ColorOverLifetime", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleColorOverLifetimeModuleDrawer>();
	});
	registerModule("ColorUV", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleColorUVModuleDrawer>();
	});
	registerModule("CustomShaderParameter", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleCustomShaderParameterModuleDrawer>();
	});
	registerModule("Emissive", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleEmissiveModuleDrawer>();
	});
	registerModule("Flipbook", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleFlipbookModuleDrawer>();
	});
	registerModule("GravityForce", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleGravityForceModuleDrawer>();
	});
	registerModule("NoiseForce", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleNoiseForceModuleDrawer>();
	});
	registerModule("NoiseUV", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleNoiseUVModuleDrawer>();
	});
	registerModule("PendulumMovement", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticlePendulumMovementModuleDrawer>();
	});
	registerModule("Rotation", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleRotationModuleDrawer>();
	});
	registerModule("ScaleOverLifetime", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleScaleOverLifetimeModuleDrawer>();
	});
	registerModule("ShapeOverLifetime", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleShapeOverLifetimeModuleDrawer>();
	});
	registerModule("SizeOverLifetime", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleSizeOverLifetimeModuleDrawer>();
	});
	registerModule("SpiralMovement", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleSpiralMovementModuleDrawer>();
	});
	registerModule("TrailColorOverLifetime", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleColorOverLifetimeModuleDrawer>();
	});
	registerModule("TrailColorUV", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleColorUVModuleDrawer>();
	});
	registerModule("TrailCustomShaderParameter", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleCustomShaderParameterModuleDrawer>();
	});
	registerModule("TrailSizeOverLifetime", []() -> std::unique_ptr<IParticleModuleDrawer> {
		return std::make_unique<ParticleSizeOverLifetimeModuleDrawer>();
	});

	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Sphere)] =
		&DrawParticleEmitterShape<ParticleSphereEmitterShape, ParticleEmitterShapeDrawers::DrawSphere>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Hemisphere)] =
		&DrawParticleEmitterShape<ParticleHemisphereEmitterShape, ParticleEmitterShapeDrawers::DrawHemisphere>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Box)] =
		&DrawParticleEmitterShape<ParticleBoxEmitterShape, ParticleEmitterShapeDrawers::DrawBox>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Torus)] =
		&DrawParticleEmitterShape<ParticleTorusEmitterShape, ParticleEmitterShapeDrawers::DrawTorus>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Circle)] =
		&DrawParticleEmitterShape<ParticleCircleEmitterShape, ParticleEmitterShapeDrawers::DrawCircle>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Cone)] =
		&DrawParticleEmitterShape<ParticleConeEmitterShape, ParticleEmitterShapeDrawers::DrawCone>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Point)] =
		&DrawParticleEmitterShape<ParticlePointEmitterShape, ParticleEmitterShapeDrawers::DrawPoint>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Rect)] =
		&DrawParticleEmitterShape<ParticleRectEmitterShape, ParticleEmitterShapeDrawers::DrawRect>;
	emitterDrawers_[static_cast<size_t>(ParticleEmitterShape::Cone2D)] =
		&DrawParticleEmitterShape<ParticleCone2DEmitterShape, ParticleEmitterShapeDrawers::DrawCone2D>;
}

Engine::ParticleEditorDescriptorRegistry& Engine::ParticleEditorDescriptorRegistry::GetInstance() {

	static ParticleEditorDescriptorRegistry instance;
	return instance;
}

bool Engine::ParticleEditorDescriptorRegistry::DrawModule(
	ParticleModuleRegistry::TypeID typeID, IParticleModule& module, IParticleModuleDrawer* drawer) const {

	if (typeID >= moduleDrawerFactories_.size()) {
		return false;
	}
	return drawer ? drawer->Draw(module) : false;
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

std::unique_ptr<Engine::IParticleModuleDrawer> Engine::ParticleEditorDescriptorRegistry::CreateModuleDrawer(
	ParticleModuleRegistry::TypeID typeID) const {

	if (typeID >= moduleDrawerFactories_.size() || !moduleDrawerFactories_[typeID]) {
		return nullptr;
	}
	return moduleDrawerFactories_[typeID]();
}
