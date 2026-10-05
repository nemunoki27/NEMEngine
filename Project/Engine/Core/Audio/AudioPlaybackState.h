#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>

namespace Engine {

	enum class AudioPlaybackOwner : uint8_t {

		Game,
		EditorPreview,
	};

	enum class AudioPauseReason : uint8_t {

		Script = 1,
		Editor = 2,
		Background = 4,
		ZeroPitch = 8,
	};

	// 複数の停止理由を保持する再生状態
	class AudioPlaybackState {
	public:

		// 実際の停止状態が変わった場合にtrueを返す
		bool SetPauseReason(AudioPauseReason reason, bool paused);
		bool IsPaused() const { return pauseReasons_ != 0; }
		bool HasPauseReason(AudioPauseReason reason) const { return (pauseReasons_ & static_cast<uint8_t>(reason)) != 0; }
	private:

		uint8_t pauseReasons_ = 0;
	};
}
