#include "ParticleGPUState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	ParticleGPUState structMethods
//============================================================================
void Engine::ParticleGPUState::Reject(const std::string& reason) {

	// 同じ診断をframeごとに出力しない
	if (!rejected || diagnostic != reason) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"GPU粒子を実行できません 内容={}", reason);
	}
	rejected = true;
	diagnostic = reason;
	pending.clear();
}
