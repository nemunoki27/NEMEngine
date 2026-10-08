#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <vector>
#include <cstdint>
// directX
#include <xaudio2.h>

namespace Engine {

	// 音源タイプ
	enum class AudioType {

		SE,
		BGM,
	};

	// 音声形式とPCMデータ
	struct AudioSoundData {

		// フォーマット情報
		std::vector<uint8_t> formatBlob;

		// PCMデータ
		std::vector<uint8_t> pcmBuffer;

		// 音源タイプ
		AudioType type{};
		// サウンドの基準音量
		float volume = 1.0f;

		// キャスト用関数
		const WAVEFORMATEX* GetFormat() const { return reinterpret_cast<const WAVEFORMATEX*>(formatBlob.data()); }
		const BYTE* GetPCM() const { return reinterpret_cast<const BYTE*>(pcmBuffer.data()); }
		uint32_t GetPCMBytes() const { return static_cast<uint32_t>(pcmBuffer.size()); }
	};
}
