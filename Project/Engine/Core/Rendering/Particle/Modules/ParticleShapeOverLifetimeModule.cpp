#include "ParticleShapeOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleShapeOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleShapeOverLifetimeModule::FromJson(const nlohmann::json& params) {

	shape_ = EnumAdapter<PrimitiveType>::FromString(
		params.value("shape", "Ring")).value_or(PrimitiveType::Ring);
	if (const auto it = params.find("ringStart"); it != params.end() && it->is_object()) { from_json(*it, ringStart_); }
	if (const auto it = params.find("ringEnd"); it != params.end() && it->is_object()) { from_json(*it, ringEnd_); }
	if (const auto it = params.find("cylinderStart"); it != params.end() && it->is_object()) { from_json(*it, cylinderStart_); }
	if (const auto it = params.find("cylinderEnd"); it != params.end() && it->is_object()) { from_json(*it, cylinderEnd_); }
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
}

nlohmann::json Engine::ParticleShapeOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["shape"] = EnumAdapter<PrimitiveType>::ToString(shape_);
	params["ringStart"] = ringStart_;
	params["ringEnd"] = ringEnd_;
	params["cylinderStart"] = cylinderStart_;
	params["cylinderEnd"] = cylinderEnd_;
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	return params;
}

void Engine::ParticleShapeOverLifetimeModule::OnSpawn(std::span<Particle> newborn) {

	for (Particle& particle : newborn) {
		ApplyShapeParams(particle, 0.0f);
	}
}

void Engine::ParticleShapeOverLifetimeModule::OnUpdate(std::span<Particle> alive, [[maybe_unused]] float deltaTime) {

	for (Particle& particle : alive) {

		const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
		ApplyShapeParams(particle, EasedValue(easingType_, progress));
	}
}

void Engine::ParticleShapeOverLifetimeModule::ApplyShapeParams(Particle& particle, float easedT) const {

	if (shape_ == PrimitiveType::Ring) {

		// 角度は度数法で持ちシェーダーへはラジアンで渡す
		particle.shapeParams = Vector4(
			Math::Lerp(ringStart_.outerRadius, ringEnd_.outerRadius, easedT),
			Math::Lerp(ringStart_.innerRadius, ringEnd_.innerRadius, easedT),
			Math::Lerp(ringStart_.startAngle, ringEnd_.startAngle, easedT) * Math::radian,
			Math::Lerp(ringStart_.endAngle, ringEnd_.endAngle, easedT) * Math::radian);
	} else if (shape_ == PrimitiveType::Cylinder) {

		particle.shapeParams = Vector4(
			Math::Lerp(cylinderStart_.topRadius, cylinderEnd_.topRadius, easedT),
			Math::Lerp(cylinderStart_.bottomRadius, cylinderEnd_.bottomRadius, easedT),
			Math::Lerp(cylinderStart_.height, cylinderEnd_.height, easedT),
			Math::Lerp(cylinderStart_.maxAngle, cylinderEnd_.maxAngle, easedT));
	}
}
