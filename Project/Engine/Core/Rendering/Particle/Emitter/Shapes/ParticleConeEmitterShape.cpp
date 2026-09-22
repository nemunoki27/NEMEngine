#include "ParticleConeEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>

// c++
#include <cmath>
#include <numbers>

//============================================================================
//	ParticleConeEmitterShape classMethods
//============================================================================
void Engine::ParticleConeEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.cone.angle = data.value("coneAngle", settings.cone.angle);
	settings.cone.radius = data.value("coneRadius", settings.cone.radius);
}

void Engine::ParticleConeEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["coneAngle"] = settings.cone.angle;
	data["coneRadius"] = settings.cone.radius;
}

void Engine::ParticleConeEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	constexpr float pi = std::numbers::pi_v<float>;
	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	// 底面円から開き角に沿って飛ばす、頂点から離れる方向にする
	const float angle = RandomGenerator::Generate(0.0f, pi * 2.0f);
	const float radius = RandomGenerator::Generate(0.0f, settings.cone.radius);
	const Vector3 radial(std::cos(angle), 0.0f, std::sin(angle));
	position = radial * radius;
	const float coneAngleRad = settings.cone.angle * degToRad;
	if (0.001f < coneAngleRad && 0.001f < settings.cone.radius) {

		const float apexDistance = settings.cone.radius / std::tan(coneAngleRad);
		direction = Vector3::Normalize(position - Vector3(0.0f, -apexDistance, 0.0f));
	} else {
		direction = Vector3(0.0f, 1.0f, 0.0f);
	}
}

void Engine::ParticleConeEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, [[maybe_unused]] bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	// 開き角に沿った上面半径で高さ1の円錐を表す
	const float displayHeight = 1.0f;
	const float topRadius = settings.cone.radius + std::tan(settings.cone.angle * degToRad) * displayHeight;
	renderer->DrawCone(center, settings.cone.radius, topRadius, displayHeight, rotation, Color4::Red());
#endif
}
