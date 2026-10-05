#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleTypes.h>

namespace Engine {

	struct ParticleRenderSettings;

	// 基本形状から粒子の初期形状を作る
	ParticleShapeData MakeParticleShapeData(const ParticleRenderSettings& settings);
}
