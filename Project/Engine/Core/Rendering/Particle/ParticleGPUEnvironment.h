#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUWorldInputs.h"
#include "ParticleFieldAssetCache.h"
#include "ParticleGPUFeedbackWorld.h"
#include <Engine/Core/World/ECS/World/ECSWorldLifetime.h>

namespace Engine {

	// Worldを跨いで実行時の表面と衝突履歴を共有しない
	struct ParticleGPUEnvironment {

		std::shared_ptr<const ECSWorldLifetime> lifetime;
		ParticleGPUWorldInputCache inputs;
		std::shared_ptr<const ParticleGPUWorldSnapshot> emptyInputs = std::make_shared<const ParticleGPUWorldSnapshot>();
		ParticleFieldAssetCache fieldAssets;
		ParticleGPUFeedbackWorld feedback;
		bool feedbackUpdate = false;
		float deltaTime = 0;
		Entity mainCamera{};
		std::vector<UUID> sceneInstances;
	};
}
