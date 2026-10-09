#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	GlobalIlluminationSettings structure
	//	Project共通の間接光設定
	//============================================================================
	struct GlobalIlluminationSettings {

		bool enabled = false;
		uint32_t quality = 1;
		float probeSpacing = 2.0f;
		float maxRayDistance = 80.0f;
		float updateBudgetMilliseconds = 3.0f;
		uint32_t debugMode = 0;
	};

	// Project設定の入出力と検証
	namespace GlobalIlluminationStorage {

		GlobalIlluminationSettings Load();
		void Save(const GlobalIlluminationSettings& settings);
		GlobalIlluminationSettings Validate(GlobalIlluminationSettings settings);
	}
} // Engine
