#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <cstdint>

namespace Engine {

	// Inspector単位でプレビューVoiceを所有する
	class AudioPreviewSession {
	public:
		~AudioPreviewSession();
		AudioPreviewSession() = default;
		AudioPreviewSession(const AudioPreviewSession&) = delete;
		AudioPreviewSession& operator=(const AudioPreviewSession&) = delete;

		// 旧Voiceを止めて指定音声を再生する
		bool Play(const std::filesystem::path& path, bool loop, float volume, float pitch);
		// このSessionが開始したVoiceだけを終了する
		void Stop();
	private:
		uint64_t voiceID_ = 0;
	};
}
