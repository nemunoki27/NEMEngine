#include "ParticlePendulumMovementModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <cmath>
#include <span>

//============================================================================
//	ParticlePendulumMovementModule classMethods
//============================================================================
void Engine::ParticlePendulumMovementModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("length"); it != params.end()) {
		ParticleFloatAnimation::ReadAnimationSettings(*it, settings_.length);
	}
	if (const auto it = params.find("maxAngle"); it != params.end()) {
		ParticleFloatAnimation::ReadAnimationSettings(*it, settings_.maxAngle);
	}
	if (const auto it = params.find("cycles"); it != params.end()) {
		ParticleFloatAnimation::ReadAnimationSettings(*it, settings_.cycles);
	}
	settings_.planeAngle = params.value("planeAngle", settings_.planeAngle);
	settings_.startPhase = params.value("startPhase", settings_.startPhase);
	settings_.particlePhaseOffset = params.value("particlePhaseOffset", settings_.particlePhaseOffset);
	settings_.arcStrength = (std::clamp)(params.value("arcStrength", settings_.arcStrength), -1.0f, 1.0f);
	settings_.reverse = params.value("reverse", settings_.reverse);
}

nlohmann::json Engine::ParticlePendulumMovementModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["length"] = ParticleFloatAnimation::WriteAnimationSettings(settings_.length);
	params["maxAngle"] = ParticleFloatAnimation::WriteAnimationSettings(settings_.maxAngle);
	params["cycles"] = ParticleFloatAnimation::WriteAnimationSettings(settings_.cycles);
	params["planeAngle"] = settings_.planeAngle;
	params["startPhase"] = settings_.startPhase;
	params["particlePhaseOffset"] = settings_.particlePhaseOffset;
	params["arcStrength"] = settings_.arcStrength;
	params["reverse"] = settings_.reverse;
	return params;
}

void Engine::ParticlePendulumMovementModule::OnSpawn(Particle& particle) {

	const PendulumBasis basis = CalculateBasis(particle);
	particle.pos += CalculateOffset(basis, CalculateStartPhase(particle.id), 0.0f);
}

void Engine::ParticlePendulumMovementModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	const float currentT = particle.age / particle.lifetime;
	const PendulumBasis basis = CalculateBasis(particle);
	const float particleStartPhase = CalculateStartPhase(particle.id);
	if (particle.phaseIndex != particle.previousPhaseIndex) {

		particle.pos += CalculateOffset(basis, particleStartPhase, currentT);
		return;
	}

	const float previousT = particle.previousAge / particle.lifetime;
	particle.pos += CalculateOffset(basis, particleStartPhase, currentT) -
		CalculateOffset(basis, particleStartPhase, previousT);
}

Engine::ParticlePendulumMovementModule::PendulumBasis Engine::ParticlePendulumMovementModule::CalculateBasis(
	const Particle& particle) const {

	const Vector3 axis = Vector3::NormalizeOr(
		particle.spawnDirection, Vector3(0.0f, 1.0f, 0.0f));
	const Vector3 reference = std::abs(Vector3::Dot(axis, Vector3(0.0f, 1.0f, 0.0f))) < 0.999f ?
		Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
	const Vector3 basisX = Vector3::NormalizeOr(
		Vector3::Cross(reference, axis), Vector3(1.0f, 0.0f, 0.0f));
	const Vector3 basisY = Vector3::NormalizeOr(
		Vector3::Cross(axis, basisX), Vector3(0.0f, 0.0f, 1.0f));
	return { axis, basisX, basisY };
}

float Engine::ParticlePendulumMovementModule::CalculateStartPhase(uint32_t particleID) const {

	const double phaseOffset = std::fmod(
		static_cast<double>(settings_.particlePhaseOffset) * static_cast<double>(particleID), 360.0);
	return settings_.startPhase + static_cast<float>(phaseOffset);
}

Engine::Vector3 Engine::ParticlePendulumMovementModule::CalculateOffset(
	const PendulumBasis& basis, float particleStartPhase, float rawT) const {

	const float length = ParticleFloatAnimation::EvaluateAnimation(settings_.length, rawT);
	const float maxAngle = ParticleFloatAnimation::EvaluateAnimation(settings_.maxAngle, rawT);
	const float cycles = ParticleFloatAnimation::EvaluateAnimation(settings_.cycles, rawT);
	const float planeAngle = Math::DegToRad(settings_.planeAngle);
	const Vector3 swingDirection =
		basis.x * std::cos(planeAngle) + basis.y * std::sin(planeAngle);
	const float direction = settings_.reverse ? -1.0f : 1.0f;
	const float phase = Math::DegToRad(particleStartPhase + cycles * 360.0f * direction);
	const float swingAngle = Math::DegToRad(maxAngle * std::sin(phase));
	return swingDirection * (std::sin(swingAngle) * length) +
		basis.axis * ((1.0f - std::cos(swingAngle)) * length * settings_.arcStrength);
}
