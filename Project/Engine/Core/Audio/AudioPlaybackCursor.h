#pragma once

//============================================================================
//	include
//============================================================================
#include "AudioSoundData.h"

namespace Engine {

	// Voiceの累積サンプル数とClip内の開始位置を対応付ける
	struct AudioPlaybackCursor {
		uint64_t clockOrigin = 0;
		uint32_t sampleOrigin = 0;
		bool reverse = false;

		// 現在のClip内サンプル位置を求める
		uint32_t Resolve(uint64_t clock, uint32_t sampleCount) const;
	};

	// チャンネルを保持してPCMのサンプル順だけを反転する
	std::vector<uint8_t> ReverseAudioPCM(const AudioSoundData& sound);
}
