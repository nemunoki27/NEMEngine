#pragma once

#include "ParticleMaterialStructures.h"
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>

#include <array>
#include <unordered_map>

namespace Engine {

	// 形状と名前付き寿命パラメータの設定
	struct ParticleShapeAnimationSettings {

		PrimitiveType shape = PrimitiveType::Ring;
		std::unordered_map<std::string, ParticleMaterialAnimatedParameter> parameters{};
	};

	namespace ParticleShapeAnimation {

	inline constexpr const char* kRingOuterRadius = "ringOuterRadius";
	inline constexpr const char* kRingInnerRadius = "ringInnerRadius";
	inline constexpr const char* kRingStartAngle = "ringStartAngle";
	inline constexpr const char* kRingEndAngle = "ringEndAngle";

	inline constexpr const char* kCylinderTopRadius = "cylinderTopRadius";
	inline constexpr const char* kCylinderCenterRadius = "cylinderCenterRadius";
	inline constexpr const char* kCylinderBottomRadius = "cylinderBottomRadius";
	inline constexpr const char* kCylinderTopRadiusWeight = "cylinderTopRadiusWeight";
	inline constexpr const char* kCylinderBottomRadiusWeight = "cylinderBottomRadiusWeight";
	inline constexpr const char* kCylinderTopColor = "cylinderTopColor";
	inline constexpr const char* kCylinderCenterColor = "cylinderCenterColor";
	inline constexpr const char* kCylinderBottomColor = "cylinderBottomColor";
	inline constexpr const char* kCylinderHeight = "cylinderHeight";
	inline constexpr const char* kCylinderMaxAngle = "cylinderMaxAngle";

	enum ParameterCacheIndex : size_t {

		RingOuterRadius,
		RingInnerRadius,
		RingStartAngle,
		RingEndAngle,
		CylinderTopRadius,
		CylinderCenterRadius,
		CylinderBottomRadius,
		CylinderTopRadiusWeight,
		CylinderBottomRadiusWeight,
		CylinderTopColor,
		CylinderCenterColor,
		CylinderBottomColor,
		CylinderHeight,
		CylinderMaxAngle,
		ParameterCount,
	};
	static_assert(ParameterCount == 14);

	inline constexpr std::array<const char*, ParameterCount> kParameterNames = {
		kRingOuterRadius,
		kRingInnerRadius,
		kRingStartAngle,
		kRingEndAngle,
		kCylinderTopRadius,
		kCylinderCenterRadius,
		kCylinderBottomRadius,
		kCylinderTopRadiusWeight,
		kCylinderBottomRadiusWeight,
		kCylinderTopColor,
		kCylinderCenterColor,
		kCylinderBottomColor,
		kCylinderHeight,
		kCylinderMaxAngle,
	};

	Engine::Vector4 ToVector4(const Engine::Color4& color);
	Engine::ParticleMaterialAnimatedParameter MakeFloatParameter(float value);
	Engine::ParticleMaterialAnimatedParameter MakeColorParameter(const Engine::Color4& value);
	Engine::Color4 ToColor4(const Engine::Vector4& value);

		// 形状に必要な既定値を補完する
		void EnsureParameters(ParticleShapeAnimationSettings& settings);
	}
}
