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

	// Media Foundationで音声をPCMへ復号する
	std::optional<AudioSoundData> ReadAudioMedia(const std::filesystem::path& path, std::string& error);
}
