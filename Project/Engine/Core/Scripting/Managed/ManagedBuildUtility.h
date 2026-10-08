#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <chrono>
#include <filesystem>
#include <string>

namespace Engine::ManagedBuildUtility {

	// パスをUTF8へ変換
	std::string ToUtf8Path(const std::filesystem::path& path);
	// MSBuildのDirectory propertyへ渡す末尾区切り付きパスへ変換する
	std::wstring ToMSBuildDirectory(const std::filesystem::path& path);
	// UTF8をOS文字列へ変換
	std::wstring Widen(const std::string& text);
	// 現在の構成名を取得
	std::string BuildProfile();
	// 現在の時刻を表示文字列へ変換
	std::string NowTimeStringUtf8();
	// 計測区間をミリ秒へ変換
	double DurationMs(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end);
}
