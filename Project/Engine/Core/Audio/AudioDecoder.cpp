#include "AudioDecoder.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include "AudioWaveReader.h"
#include "AudioMediaReader.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <utility>
#include <stdexcept>
#include <string>
// windows
#include <mfapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

void AudioDecoder::Init() {

	HRESULT hr = MFStartup(MF_VERSION);
	if (SUCCEEDED(hr)) {
		mfStarted_ = true;
	} else {
		throw std::runtime_error("Media Foundationの開始に失敗しました HRESULT=" + std::to_string(hr));
	}
}

void AudioDecoder::Finalize() {

	if (mfStarted_) {
		MFShutdown();
		mfStarted_ = false;
	}
}

AudioSoundData AudioDecoder::LoadWaveFile(const std::filesystem::path& filename) {

	// 読込に成功した音声データだけを返す
	std::string error;
	bool requiresDecode = false;
	if (auto sound = ReadAudioWave(filename, error, &requiresDecode)) {
		return std::move(*sound);
	}
	if (requiresDecode && mfStarted_) {
		if (auto sound = ReadAudioMedia(filename, error)) { return std::move(*sound); }
	}
	Logger::Output(LogType::Engine, spdlog::level::warn,
		"Audio: WAVを読み込めません path={} 内容={}", Algorithm::PathToUTF8(filename), error);
	return {};
}
AudioSoundData AudioDecoder::LoadMP3File(const std::filesystem::path& filename) {

	// 破損ファイルの失敗は旧PCMを残して通知する
	std::string error;
	if (mfStarted_) {
		if (auto sound = ReadAudioMedia(filename, error)) { return std::move(*sound); }
	} else {
		error = "Media Foundationが開始されていません";
	}
	Logger::Output(LogType::Engine, spdlog::level::warn,
		"Audio: 音声を復号できません path={} 内容={}", Algorithm::PathToUTF8(filename), error);
	return {};
}
