#pragma once

//============================================================================
//	include
//============================================================================
#include "AudioSoundData.h"

// c++
#include <filesystem>
#include <optional>
#include <string>

namespace Engine {

	// WAVの境界と形式を検証して音声データを作る
	std::optional<AudioSoundData> ReadAudioWave(const std::filesystem::path& path, std::string& error,
		bool* requiresDecode = nullptr);
}
