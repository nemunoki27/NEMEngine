#pragma once

//============================================================================
//	include
//============================================================================
// directX
#include <xaudio2.h>
// c++
#include <memory>

namespace Engine {

	// Source Voiceの終了処理
	struct AudioVoiceDeleter {

		void operator()(IXAudio2SourceVoice* voice) const;
	};

	using AudioVoiceOwner = std::unique_ptr<IXAudio2SourceVoice, AudioVoiceDeleter>;
}
