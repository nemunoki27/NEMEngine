#include "AudioDevice.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

#pragma comment(lib, "xaudio2.lib")

void AudioDevice::Init() {

	HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	Assert::Call(SUCCEEDED(hr), "XAudio2の初期化に失敗しました");

	hr = xAudio2_->CreateMasteringVoice(&masteringVoice_);
	Assert::Call(SUCCEEDED(hr), "XAudio2のマスタリングボイス作成に失敗しました");

	hr = xAudio2_->StartEngine();
	Assert::Call(SUCCEEDED(hr), "XAudio2の再生エンジン開始に失敗しました");

}

void AudioDevice::Finalize() {

	if (masteringVoice_) {
		masteringVoice_->DestroyVoice();
		masteringVoice_ = nullptr;
	}
	if (xAudio2_) {
		xAudio2_->StopEngine();
		xAudio2_.Reset();
	}
}

HRESULT AudioDevice::CreateSourceVoice(IXAudio2SourceVoice** voice, const WAVEFORMATEX* format) {

	return xAudio2_->CreateSourceVoice(voice, format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, nullptr, nullptr);
}
