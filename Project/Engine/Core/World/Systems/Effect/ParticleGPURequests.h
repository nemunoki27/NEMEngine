#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>

namespace Engine {

	// GPU再生の定義と更新要求を同期する
	bool QueueParticleGPUUpdate(ParticleInstanceState& instance, const Matrix4x4& emitterWorld,
		const ParticlePhaseParentSettings& parentSettings, bool usePhaseSettings,
		float deltaTime, bool updateSimulation, bool emissionEnabled);
}
