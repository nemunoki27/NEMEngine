#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <string>

namespace Engine::Algorithm {

	// 環境変数の文字列をOSのパスとして取得
	std::filesystem::path GetEnvironmentPath(const std::wstring& name);

	// 実行中のEXEの絶対パスを取得
	std::filesystem::path GetExecutablePath();

	// 不正な文字列と途中の終端を拒否してパスへ変換する
	std::filesystem::path PathFromUTF8(const std::string& path);

	// OSのパスをUTF8へ変換する
	std::string PathToUTF8(const std::filesystem::path& path);

	// 長さ制限を避けるOSの絶対パスへ変換する
	std::filesystem::path ToFileSystemPath(const std::filesystem::path& path);
}
