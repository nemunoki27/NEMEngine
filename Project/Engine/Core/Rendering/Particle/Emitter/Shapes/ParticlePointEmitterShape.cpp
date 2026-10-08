#include "ParticlePointEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

//============================================================================
//	ParticlePointEmitterShape classMethods
//============================================================================
void Engine::ParticlePointEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	if (const auto it = data.find("pointDirection"); it != data.end()) { settings.point.direction = Vector3::FromJson(*it); }
}

void Engine::ParticlePointEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["pointDirection"] = settings.point.direction.ToJson();
}

void Engine::ParticlePointEmitterShape::InitParticle([[maybe_unused]] Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	// 原点から指定方向へ飛ばす
	direction = Vector3::NormalizeOr(settings.point.direction, Vector3(0.0f, 1.0f, 0.0f));
}
