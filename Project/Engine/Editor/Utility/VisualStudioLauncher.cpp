#include "VisualStudioLauncher.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <system_error>

// windows
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace {

	// インストール済みVisual Studioの実行ファイルを探す
	std::filesystem::path FindVisualStudioExecutable() {

		const std::filesystem::path roots[] = {
			Engine::Algorithm::GetEnvironmentPath(L"ProgramFiles"),
			Engine::Algorithm::GetEnvironmentPath(L"ProgramFiles(x86)"),
		};
		const char* versions[] = { "18", "17", "16" };
		const char* editions[] = { "Community", "Professional", "Enterprise", "Preview" };

		for (const auto& root : roots) {
			if (root.empty()) {
				continue;
			}
			for (const char* version : versions) {
				for (const char* edition : editions) {

					const auto executable = root / "Microsoft Visual Studio" / version / edition / "Common7/IDE/devenv.exe";
					std::error_code ec;
					if (std::filesystem::is_regular_file(executable, ec)) {
						return executable;
					}
				}
			}
		}
		return {};
	}
}

//============================================================================
//	VisualStudioLauncher namespaceMethods
//============================================================================
bool Engine::VisualStudioLauncher::OpenFile(const std::filesystem::path& file) {

	// 欠損したソースやフォルダは開かない
	std::error_code ec;
	if (file.empty() || !std::filesystem::is_regular_file(file, ec)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[VisualStudioLauncher] ソースファイルが見つかりません: {}", Algorithm::PathToUTF8(file));
		return false;
	}
	const auto executable = FindVisualStudioExecutable();
	if (executable.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "[VisualStudioLauncher] Visual Studioが見つかりません");
		return false;
	}

	// 日本語や空白を含むパスを既存のウィンドウへ渡す
	const std::wstring parameters = L"/Edit \"" + file.wstring() + L"\"";
	const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", executable.c_str(),
		parameters.c_str(), nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) > 32) {
		return true;
	}
	Logger::Output(LogType::Engine, spdlog::level::warn,
		"[VisualStudioLauncher] ソースファイルを開けません: {}", Algorithm::PathToUTF8(file));
	return false;
}
