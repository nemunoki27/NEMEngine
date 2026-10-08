#include "AudioDevice.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#pragma comment(lib, "xaudio2.lib")

void AudioDevice::Init() {

	auto requireSuccess = [](HRESULT result, const char* message) {
		if (FAILED(result)) { throw std::runtime_error(std::string(message) + " HRESULT=" + std::to_string(result)); }
	};

	HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	requireSuccess(hr, "XAudio2の初期化に失敗しました");

	hr = xAudio2_->CreateMasteringVoice(&masteringVoice_);
	requireSuccess(hr, "XAudio2のマスタリングボイス作成に失敗しました");
	// 実機のスピーカー構成で3D定位を初期化する
	DWORD speakerMask = 0;
	hr = masteringVoice_->GetChannelMask(&speakerMask);
	requireSuccess(hr, "XAudio2のスピーカー構成を取得できませんでした");
	hr = X3DAudioInitialize(speakerMask, X3DAUDIO_SPEED_OF_SOUND, spatialHandle_);
	requireSuccess(hr, "X3DAudioの初期化に失敗しました");
	XAUDIO2_VOICE_DETAILS details{};
	masteringVoice_->GetVoiceDetails(&details);
	outputChannels_ = details.InputChannels;

	hr = xAudio2_->StartEngine();
	requireSuccess(hr, "XAudio2の再生エンジン開始に失敗しました");

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

	return xAudio2_->CreateSourceVoice(voice, format, 0, XAUDIO2_MAX_FREQ_RATIO, nullptr, nullptr, nullptr);
}

std::vector<float> AudioDevice::GetOutputMatrix(IXAudio2SourceVoice& voice, uint32_t channels) const {

	std::vector<float> matrix(channels * outputChannels_);
	voice.GetOutputMatrix(masteringVoice_, channels, outputChannels_, matrix.data());
	return matrix;
}

void AudioDevice::ApplySpatialMatrix(IXAudio2SourceVoice& voice, uint32_t channels,
	const std::vector<float>& normalMatrix, const AudioSpatialState& source, const AudioListenerState& listener) const {

	float blend = std::isfinite(source.blend) ? std::clamp(source.blend, 0.0f, 1.0f) : 0.0f;
	std::vector<float> matrix = normalMatrix;
	// Listenerがない場合も2D成分は保持する
	float gain = CalculateAudioDistanceGain(source, listener);
	if (blend > 0.0f && gain > 0.0f) {
		X3DAUDIO_LISTENER receiver{};
		receiver.Position = { listener.position.x, listener.position.y, listener.position.z };
		receiver.OrientFront = { listener.forward.x, listener.forward.y, listener.forward.z };
		receiver.OrientTop = { listener.up.x, listener.up.y, listener.up.z };
		X3DAUDIO_EMITTER emitter{};
		emitter.Position = { source.position.x, source.position.y, source.position.z };
		emitter.OrientFront = { 0.0f, 0.0f, 1.0f };
		emitter.OrientTop = { 0.0f, 1.0f, 0.0f };
		emitter.ChannelCount = channels;
		std::vector<float> azimuths(channels);
		for (uint32_t i = 0; i < channels; ++i) {
			azimuths[i] = X3DAUDIO_2PI * static_cast<float>(i) / static_cast<float>(channels);
		}
		emitter.pChannelAzimuths = channels > 1 ? azimuths.data() : nullptr;
		// 距離減衰は共通の計算結果を使う
		X3DAUDIO_DISTANCE_CURVE_POINT points[]{ { 0.0f, 1.0f }, { 1.0f, 1.0f } };
		X3DAUDIO_DISTANCE_CURVE curve{ points, 2 };
		emitter.pVolumeCurve = &curve;
		emitter.pLFECurve = &curve;
		emitter.CurveDistanceScaler = 1.0f;
		std::vector<float> spatial(matrix.size());
		X3DAUDIO_DSP_SETTINGS settings{};
		settings.SrcChannelCount = channels;
		settings.DstChannelCount = outputChannels_;
		settings.pMatrixCoefficients = spatial.data();
		X3DAudioCalculate(spatialHandle_, &receiver, &emitter, X3DAUDIO_CALCULATE_MATRIX, &settings);
		for (size_t i = 0; i < matrix.size(); ++i) {
			matrix[i] = matrix[i] * (1.0f - blend) + spatial[i] * gain * blend;
		}
	} else {
		for (float& value : matrix) { value *= 1.0f - blend; }
	}
	voice.SetOutputMatrix(masteringVoice_, channels, outputChannels_, matrix.data(), XAUDIO2_COMMIT_NOW);
}
