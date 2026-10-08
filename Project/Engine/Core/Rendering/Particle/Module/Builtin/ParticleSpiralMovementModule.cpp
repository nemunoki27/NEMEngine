#include "ParticleSpiralMovementModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <cmath>
#include <span>

//============================================================================
//	ParticleSpiralMovementModule classMethods
//============================================================================
void Engine::ParticleSpiralMovementModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("radius"); it != params.end()) {
		ParticleFloatAnimation::ReadAnimationSettings(*it, settings_.radius);
	}
	if (const auto it = params.find("turns"); it != params.end()) {
		ParticleFloatAnimation::ReadAnimationSettings(*it, settings_.turns);
	}
	settings_.startAngle = params.value("startAngle", settings_.startAngle);
	settings_.particleAngleOffset = params.value("particleAngleOffset", settings_.particleAngleOffset);
	settings_.reverse = params.value("reverse", settings_.reverse);
}

nlohmann::json Engine::ParticleSpiralMovementModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["radius"] = ParticleFloatAnimation::WriteAnimationSettings(settings_.radius);
	params["turns"] = ParticleFloatAnimation::WriteAnimationSettings(settings_.turns);
	params["startAngle"] = settings_.startAngle;
	params["particleAngleOffset"] = settings_.particleAngleOffset;
	params["reverse"] = settings_.reverse;
	return params;
}

void Engine::ParticleSpiralMovementModule::OnSpawn(Particle& particle) {

	const SpiralBasis basis = CalculateBasis(particle);
	particle.pos += CalculateOffset(basis, CalculateStartAngle(particle.id), 0.0f);
}

void Engine::ParticleSpiralMovementModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	const float currentT = particle.age / particle.lifetime;
	const SpiralBasis basis = CalculateBasis(particle);
	const float particleStartAngle = CalculateStartAngle(particle.id);
	if (particle.phaseIndex != particle.previousPhaseIndex) {

		particle.pos += CalculateOffset(basis, particleStartAngle, currentT);
		return;
	}

	const float previousT = particle.previousAge / particle.lifetime;
	particle.pos += CalculateOffset(basis, particleStartAngle, currentT) -
		CalculateOffset(basis, particleStartAngle, previousT);
}

Engine::ParticleSpiralMovementModule::SpiralBasis Engine::ParticleSpiralMovementModule::CalculateBasis(
	const Particle& particle) const {

	const Vector3 axis = Vector3::NormalizeOr(
		particle.spawnDirection, Vector3(0.0f, 1.0f, 0.0f));
	const Vector3 reference = std::abs(Vector3::Dot(axis, Vector3(0.0f, 1.0f, 0.0f))) < 0.999f ?
		Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
	const Vector3 basisX = Vector3::NormalizeOr(
		Vector3::Cross(reference, axis), Vector3(1.0f, 0.0f, 0.0f));
	const Vector3 basisY = Vector3::NormalizeOr(
		Vector3::Cross(axis, basisX), Vector3(0.0f, 0.0f, 1.0f));
	return { basisX, basisY };
}

float Engine::ParticleSpiralMovementModule::CalculateStartAngle(uint32_t particleID) const {

	const double angleOffset = std::fmod(
		static_cast<double>(settings_.particleAngleOffset) * static_cast<double>(particleID), 360.0);
	return settings_.startAngle + static_cast<float>(angleOffset);
}

Engine::Vector3 Engine::ParticleSpiralMovementModule::CalculateOffset(
	const SpiralBasis& basis, float particleStartAngle, float rawT) const {

	const float radius = ParticleFloatAnimation::EvaluateAnimation(settings_.radius, rawT);
	const float turns = ParticleFloatAnimation::EvaluateAnimation(settings_.turns, rawT);
	const float direction = settings_.reverse ? -1.0f : 1.0f;
	const float angle = Math::DegToRad(particleStartAngle + turns * 360.0f * direction);
	return (basis.x * std::cos(angle) + basis.y * std::sin(angle)) * radius;
}
