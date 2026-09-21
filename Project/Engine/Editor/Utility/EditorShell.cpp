#include "EditorShell.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <system_error>

// windows
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

//============================================================================
//	EditorShell methods
//============================================================================
bool Engine::EditorShell::OpenWithSystemDefault(const std::filesystem::path& file) {

	std::error_code ec{};
	if (file.empty() || !std::filesystem::exists(file, ec) || ec) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[EditorShell] 対象ファイルが存在しません: {}", Algorithm::PathToUTF8(file));
		return false;
	}

	const HINSTANCE result = ::ShellExecuteW(
		nullptr, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) > 32) {
		return true;
	}

	Logger::Output(LogType::Engine, spdlog::level::warn,
		"[EditorShell] 既定アプリでファイルを開けません: {}", Algorithm::PathToUTF8(file));
	return false;
}

bool Engine::EditorShell::OpenDirectory(const std::filesystem::path& directory) {

	std::error_code ec{};
	if (directory.empty() || !std::filesystem::is_directory(directory, ec) || ec) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[EditorShell] 対象Directoryが存在しません: {}", Algorithm::PathToUTF8(directory));
		return false;
	}

	const std::wstring directoryW = directory.wstring();
	const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", directoryW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) > 32) {
		return true;
	}

	Logger::Output(LogType::Engine, spdlog::level::warn,
		"[EditorShell] ExplorerでDirectoryを開けません: {}", Algorithm::PathToUTF8(directory));
	return false;
}
