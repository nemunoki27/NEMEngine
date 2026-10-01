#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace Engine::ManagedBuildUtility {

	std::string ToUtf8Path(const std::filesystem::path& path);
	// MSBuildのDirectory propertyへ渡す末尾区切り付きパスへ変換する
	std::wstring ToMSBuildDirectory(const std::filesystem::path& path);
	std::wstring Widen(const std::string& text);
	std::string BuildProfile();
	std::string NowTimeStringUtf8();
	double DurationMs(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end);
}
