#include "ParticleShapeOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <array>
#include <span>

//============================================================================
//	ParticleShapeOverLifetimeModule internal
//============================================================================
using namespace Engine::ParticleShapeAnimation;

//============================================================================
//	ParticleShapeOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleShapeOverLifetimeModule::FromJson(const nlohmann::json& params) {

	settings_.parameters.clear();
	const nlohmann::json safeParams = params.is_object() ? params : nlohmann::json::object();
	settings_.shape = EnumAdapter<PrimitiveType>::FromString(
		safeParams.value("shape", "Ring")).value_or(PrimitiveType::Ring);

	if (const auto it = safeParams.find("parameters"); it != safeParams.end() && it->is_object()) {
		for (auto parameterIt = it->begin(); parameterIt != it->end(); ++parameterIt) {

			ParticleMaterialAnimatedParameter parameter{};
			from_json(parameterIt.value(), parameter);
			settings_.parameters[parameterIt.key()] = std::move(parameter);
		}
	}
	ParticleShapeAnimation::EnsureParameters(settings_);
	RebuildParameterCache();
}

nlohmann::json Engine::ParticleShapeOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["shape"] = EnumAdapter<PrimitiveType>::ToString(settings_.shape);
	params["parameters"] = nlohmann::json::object();
	for (const auto& [name, parameter] : settings_.parameters) {
		to_json(params["parameters"][name], parameter);
	}
	return params;
}

void Engine::ParticleShapeOverLifetimeModule::OnSpawn(Particle& particle) {

	EvaluateShape(particle, 0.0f);
}

void Engine::ParticleShapeOverLifetimeModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = particle.lifetime <= 0.0f ? 1.0f : particle.age / particle.lifetime;
	EvaluateShape(particle, progress);
}

void Engine::ParticleShapeOverLifetimeModule::RebuildParameterCache() {

	parameterCache_.fill(nullptr);
	for (size_t index = 0; index < kParameterNames.size(); ++index) {

		const auto it = settings_.parameters.find(kParameterNames[index]);
		if (it != settings_.parameters.end()) {
			parameterCache_[index] = &it->second;
		}
	}
}

void Engine::ParticleShapeOverLifetimeModule::EvaluateShape(Particle& particle, float progress) const {

	auto evaluate = [&](size_t index, const Vector4& defaultValue) {

		const ParticleMaterialAnimatedParameter* parameter = parameterCache_[index];
		return parameter ?
			EvaluateParticleMaterialParameter(*parameter, progress) : defaultValue;
	};

	particle.shapeData = ParticleShapeData{};
	if (settings_.shape == PrimitiveType::Ring) {

		const float outerRadius = (std::max)(evaluate(RingOuterRadius, Vector4(1.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
		const float innerRadius = (std::max)(evaluate(RingInnerRadius, Vector4(0.5f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
		const float startAngle = std::clamp(evaluate(RingStartAngle, Vector4{}).x, 0.0f, 360.0f);
		const float endAngle = std::clamp(evaluate(RingEndAngle, Vector4(360.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f, 360.0f);
		particle.shapeData.params0 = Vector4(
			outerRadius, innerRadius, startAngle * Math::radian, endAngle * Math::radian);
		return;
	}
	if (settings_.shape != PrimitiveType::Cylinder) {
		return;
	}

	const float topRadius = (std::max)(evaluate(CylinderTopRadius, Vector4(1.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
	const float centerRadius = (std::max)(evaluate(CylinderCenterRadius, Vector4(1.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
	const float bottomRadius = (std::max)(evaluate(CylinderBottomRadius, Vector4(1.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
	const float topWeight = std::clamp(evaluate(CylinderTopRadiusWeight, Vector4{}).x, 0.0f, 1.0f);
	const float bottomWeight = std::clamp(evaluate(CylinderBottomRadiusWeight, Vector4{}).x, 0.0f, 1.0f);
	const float height = (std::max)(evaluate(CylinderHeight, Vector4(2.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
	const float maxAngle = std::clamp(
		evaluate(CylinderMaxAngle, Vector4(360.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f, 360.0f);

	particle.shapeData.params0 = Vector4(topRadius, centerRadius, bottomRadius, height);
	particle.shapeData.params1 = Vector4(topWeight, bottomWeight, maxAngle * Math::radian, 1.0f);
	particle.shapeData.topColor = ToColor4(evaluate(CylinderTopColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f)));
	particle.shapeData.centerColor = ToColor4(evaluate(CylinderCenterColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f)));
	particle.shapeData.bottomColor = ToColor4(evaluate(CylinderBottomColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f)));
}

void Engine::ParticleShapeOverLifetimeModule::SetSettings(const ParticleShapeAnimationSettings& settings) {

	settings_ = settings;
	RebuildParameterCache();
}
