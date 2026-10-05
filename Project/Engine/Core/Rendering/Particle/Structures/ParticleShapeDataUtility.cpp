#include "ParticleShapeDataUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Foundation/Math/Math.h>

Engine::ParticleShapeData Engine::MakeParticleShapeData(const ParticleRenderSettings& settings) {

	ParticleShapeData data{};
	if (settings.shape == PrimitiveType::Ring) {

		// 半径と角度をGPUと共有する形状値へ変換する
		data.params0 = Vector4(settings.ring.outerRadius, settings.ring.innerRadius,
			settings.ring.startAngle * Math::radian, settings.ring.endAngle * Math::radian);
	} else if (settings.shape == PrimitiveType::Cylinder) {

		const PrimitiveCylinderParams& cylinder = settings.cylinder;
		data.params0 = Vector4(cylinder.topRadius, cylinder.centerRadius, cylinder.bottomRadius, cylinder.height);
		data.params1 = Vector4(cylinder.topRadiusWeight, cylinder.bottomRadiusWeight,
			cylinder.maxAngle * Math::radian, 1.0f);
		data.topColor = cylinder.topColor;
		data.centerColor = cylinder.centerColor;
		data.bottomColor = cylinder.bottomColor;
	}
	return data;
}
