#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// directX
#include <xaudio2.h>

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

		//--------- accessor -----------------------------------------------------

		bool IsInitialized() const { return xAudio2_ && masteringVoice_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<IXAudio2> xAudio2_{};
		IXAudio2MasteringVoice* masteringVoice_ = nullptr;
	};
}
