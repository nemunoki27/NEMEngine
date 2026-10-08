#include "AnimationClipAsset.h"

//============================================================================
//	include
//============================================================================
#include "AnimationChannelUtility.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// c++
#include <algorithm>
#include <array>
#include <fstream>

using namespace Engine::AnimationChannelUtility;

bool Engine::LoadAnimationClipAsset(const std::filesystem::path& path, AnimationClipAsset& outClip) {

	// AnimationClipはjson単体で管理する
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	try {
		nlohmann::json data{};
		file >> data;
		outClip = data.get<AnimationClipAsset>();
		return true;
	} catch (const std::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"AnimationClipAssetの読み込みに失敗しました path={} 内容={}", path.string(), e.what());
		return false;
	}
}

bool Engine::SaveAnimationClipAsset(const std::filesystem::path& path, const AnimationClipAsset& clip) {

	try {
		// ProjectPanelから作られた直後でも保存できるよう、親フォルダを先に作る
		std::filesystem::create_directories(path.parent_path());

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}

		nlohmann::json data = clip;
		file << data.dump(2);
		return true;
	} catch (const std::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"AnimationClipAssetの保存に失敗しました path={} 内容={}", path.string(), e.what());
		return false;
	}
}
