#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleSystemSettings.h"

// c++
#include <string_view>

namespace Engine {

	// World入力を使うGPU専用Moduleか確認する
	bool IsParticleModuleGPUOnly(std::string_view moduleID);
	// 実行対象が扱えない設定を停止前に診断する
	std::string ValidateParticleSimulationSettings(const ParticleSystemSettings& settings, ParticleSimulationTarget target);
}
