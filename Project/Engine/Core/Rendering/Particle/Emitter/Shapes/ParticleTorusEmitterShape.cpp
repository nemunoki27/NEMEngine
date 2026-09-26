#include "ParticleTorusEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <cmath>
#include <numbers>

//============================================================================
//	ParticleTorusEmitterShape classMethods
//============================================================================
void Engine::ParticleTorusEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.torus.radius = data.value("torusRadius", settings.torus.radius);
	settings.torus.thickness = data.value("torusThickness", settings.torus.thickness);
}

void Engine::ParticleTorusEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["torusRadius"] = settings.torus.radius;
	data["torusThickness"] = settings.torus.thickness;
}

void Engine::ParticleTorusEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	constexpr float pi = std::numbers::pi_v<float>;

	// 主円周上の管内から管の外向きに飛ばす
	const float mainAngle = RandomGenerator::Generate(0.0f, pi * 2.0f);
	const float tubeAngle = RandomGenerator::Generate(0.0f, pi * 2.0f);
	const float tubeRadius = RandomGenerator::Generate(0.0f, settings.torus.thickness);
	const Vector3 radial(std::cos(mainAngle), 0.0f, std::sin(mainAngle));
	const Vector3 tubeDir = radial * std::cos(tubeAngle) + Vector3(0.0f, std::sin(tubeAngle), 0.0f);
	position = radial * settings.torus.radius + tubeDir * tubeRadius;
	direction = tubeDir;
}
