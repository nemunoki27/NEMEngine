#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	enum class AudioRolloffMode {
		Logarithmic,
		Linear,
	};

	// Cameraから独立した受音位置
	struct AudioListenerState {
		Vector3 position{};
		Vector3 forward{ 0.0f, 0.0f, 1.0f };
		Vector3 up{ 0.0f, 1.0f, 0.0f };
		bool active = false;
	};

	// Voiceへ渡す空間設定
	struct AudioSpatialState {
		Vector3 position{};
		float blend = 0.0f;
		float minDistance = 1.0f;
		float maxDistance = 500.0f;
		AudioRolloffMode rolloffMode = AudioRolloffMode::Logarithmic;
	};

	// 不正値を補正して距離減衰を求める
	float CalculateAudioDistanceGain(const AudioSpatialState& source, const AudioListenerState& listener);
}
