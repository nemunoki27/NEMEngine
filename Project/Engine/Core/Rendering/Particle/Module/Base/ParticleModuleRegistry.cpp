#include "ParticleModuleRegistry.h"

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

//============================================================================
//	ParticleModuleRegistry classMethods
//============================================================================
namespace {

	template<typename T>
	std::unique_ptr<Engine::IParticleModule> CreateParticleModule() {

		return std::make_unique<T>();
	}
}

Engine::ParticleModuleRegistry::ParticleModuleRegistry() {

	Register("AlphaReference", &CreateParticleModule<ParticleAlphaReferenceModule>);
	Register("ColorOverLifetime", &CreateParticleModule<ParticleColorOverLifetimeModule>);
	Register("ColorUV", &CreateParticleModule<ParticleColorUVModule>);
	Register("CustomShaderParameter", &CreateParticleModule<ParticleCustomShaderParameterModule>);
	Register("Emissive", &CreateParticleModule<ParticleEmissiveModule>);
	Register("Flipbook", &CreateParticleModule<ParticleFlipbookModule>);
	Register("GravityForce", &CreateParticleModule<ParticleGravityForceModule>);
	Register("LookToVelocity", &CreateParticleModule<ParticleLookToVelocityModule>);
	Register("NoiseForce", &CreateParticleModule<ParticleNoiseForceModule>);
	Register("NoiseUV", &CreateParticleModule<ParticleNoiseUVModule>);
	Register("PendulumMovement", &CreateParticleModule<ParticlePendulumMovementModule>);
	Register("Rotation", &CreateParticleModule<ParticleRotationModule>);
	Register("ScaleOverLifetime", &CreateParticleModule<ParticleScaleOverLifetimeModule>);
	Register("ShapeOverLifetime", &CreateParticleModule<ParticleShapeOverLifetimeModule>);
	Register("SizeOverLifetime", &CreateParticleModule<ParticleSizeOverLifetimeModule>);
	Register("SpiralMovement", &CreateParticleModule<ParticleSpiralMovementModule>);
	Register("TrailColorOverLifetime", &CreateParticleModule<ParticleTrailColorOverLifetimeModule>);
	Register("TrailColorUV", &CreateParticleModule<ParticleTrailColorUVModule>);
	Register("TrailCustomShaderParameter", &CreateParticleModule<ParticleTrailCustomShaderParameterModule>);
	Register("TrailSizeOverLifetime", &CreateParticleModule<ParticleTrailSizeOverLifetimeModule>);
}

Engine::ParticleModuleRegistry& Engine::ParticleModuleRegistry::GetInstance() {

	static ParticleModuleRegistry instance;
	return instance;
}

Engine::ParticleModuleRegistry::TypeID Engine::ParticleModuleRegistry::Register(
	std::string id, CreateFunc create) {

	const auto found = typeIDs_.find(id);
	if (found != typeIDs_.end()) {
		return found->second;
	}
	if (!create || descriptors_.size() >= kInvalidTypeID) {
		return kInvalidTypeID;
	}

	const TypeID typeID = static_cast<TypeID>(descriptors_.size());
	typeIDs_.emplace(id, typeID);
	descriptors_.emplace_back(Descriptor{
		.typeID = typeID,
		.id = std::move(id),
		.create = create,
		});
	return typeID;
}

std::unique_ptr<Engine::IParticleModule> Engine::ParticleModuleRegistry::Create(
	const std::string& id) const {

	return Create(FindTypeID(id));
}

std::unique_ptr<Engine::IParticleModule> Engine::ParticleModuleRegistry::Create(
	TypeID typeID) const {

	if (typeID == kInvalidTypeID || typeID >= descriptors_.size()) {
		return nullptr;
	}
	const CreateFunc create = descriptors_[typeID].create;
	return create ? create() : nullptr;
}

Engine::ParticleModuleRegistry::TypeID Engine::ParticleModuleRegistry::FindTypeID(
	const std::string& id) const {

	const auto found = typeIDs_.find(id);
	return found != typeIDs_.end() ? found->second : kInvalidTypeID;
}
