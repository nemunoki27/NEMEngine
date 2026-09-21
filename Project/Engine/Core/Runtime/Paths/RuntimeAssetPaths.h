#pragma once

//============================================================================
//	include
//============================================================================
#include "RuntimePaths.h"

namespace Engine::RuntimePathDetail {

	// 確定したパス情報を使ってURIと実パスを変換する
	std::filesystem::path ResolveVirtualPath(const RuntimePaths::PathState& state, std::string_view virtualPath);

	std::string ToVirtualPath(const RuntimePaths::PathState& state, const std::filesystem::path& fullPath);

	std::filesystem::path ResolveAssetPath(const RuntimePaths::PathState& state, const std::filesystem::path& assetPath);

	std::string ToAssetPath(const RuntimePaths::PathState& state, const std::filesystem::path& fullPath);
}
