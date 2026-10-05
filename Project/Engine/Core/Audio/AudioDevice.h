#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include "AudioSpatialState.h"

// directX
#include <xaudio2.h>
#include <x3daudio.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	AudioDevice class
	//	XAudio2とマスターボイスを所有する
	//============================================================================
	class AudioDevice {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// 再生機器を初期化
		void Init();
		// 再生機器を終了
		void Finalize();
		// 再生ボイスを作成
		HRESULT CreateSourceVoice(IXAudio2SourceVoice** voice, const WAVEFORMATEX* format);
		// Voiceの既定の出力行列を取得する
		std::vector<float> GetOutputMatrix(IXAudio2SourceVoice& voice, uint32_t channels) const;
		// 既定の2D行列と3D定位を混合する
		void ApplySpatialMatrix(IXAudio2SourceVoice& voice, uint32_t channels, const std::vector<float>& normalMatrix,
			const AudioSpatialState& source, const AudioListenerState& listener) const;

		//--------- accessor -----------------------------------------------------

		bool IsInitialized() const { return xAudio2_ && masteringVoice_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<IXAudio2> xAudio2_{};
		IXAudio2MasteringVoice* masteringVoice_ = nullptr;
		X3DAUDIO_HANDLE spatialHandle_{};
		uint32_t outputChannels_ = 0;
	};
}
