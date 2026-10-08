#include "ParticleShapeAnimationSettings.h"

namespace Engine::ParticleShapeAnimation {

	Engine::Vector4 ToVector4(const Engine::Color4& color) {

		return Engine::Vector4(color.r, color.g, color.b, color.a);
	}

	Engine::ParticleMaterialAnimatedParameter MakeFloatParameter(float value) {

		Engine::ParticleMaterialAnimatedParameter parameter{};
		parameter.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		parameter.constant.x = value;
		parameter.start.x = value;
		parameter.end.x = value;
		parameter.componentCount = 1;
		return parameter;
	}

	Engine::ParticleMaterialAnimatedParameter MakeColorParameter(const Engine::Color4& value) {

		Engine::ParticleMaterialAnimatedParameter parameter{};
		parameter.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		parameter.constant = ToVector4(value);
		parameter.start = parameter.constant;
		parameter.end = parameter.constant;
		parameter.componentCount = 4;
		return parameter;
	}

	Engine::Color4 ToColor4(const Engine::Vector4& value) {

		return Engine::Color4(value.x, value.y, value.z, value.w);
	}

void EnsureParameters(ParticleShapeAnimationSettings& settings) {

	auto ensureFloat = [&](const char* key, float value) {

		auto it = settings.parameters.try_emplace(key, MakeFloatParameter(value)).first;
		it->second.componentCount = 1;
	};
	auto ensureColor = [&](const char* key, const Color4& value) {

		auto it = settings.parameters.try_emplace(key, MakeColorParameter(value)).first;
		it->second.componentCount = 4;
	};

	if (settings.shape == PrimitiveType::Ring) {

		ensureFloat(kRingOuterRadius, 1.0f);
		ensureFloat(kRingInnerRadius, 0.5f);
		ensureFloat(kRingStartAngle, 0.0f);
		ensureFloat(kRingEndAngle, 360.0f);
	} else if (settings.shape == PrimitiveType::Cylinder) {

		ensureFloat(kCylinderTopRadius, 1.0f);
		ensureFloat(kCylinderCenterRadius, 1.0f);
		ensureFloat(kCylinderBottomRadius, 1.0f);
		ensureFloat(kCylinderTopRadiusWeight, 0.0f);
		ensureFloat(kCylinderBottomRadiusWeight, 0.0f);
		ensureColor(kCylinderTopColor, Color4::White());
		ensureColor(kCylinderCenterColor, Color4::White());
		ensureColor(kCylinderBottomColor, Color4::White());
		ensureFloat(kCylinderHeight, 2.0f);
		ensureFloat(kCylinderMaxAngle, 360.0f);
	}
}
}
