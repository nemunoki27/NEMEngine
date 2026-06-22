#include "EditorShell.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <system_error>

// windows
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

//============================================================================
//	EditorShell anonymous
//============================================================================
namespace {

	// UTF8をワイド文字へ変換する、ShellExecuteWへ渡すため
	std::wstring Widen(const std::string& text) {

		if (text.empty()) {
			return std::wstring();
		}
		const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring result(static_cast<size_t>(size), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
		return result;
	}
}

//============================================================================
//	EditorShell methods
//============================================================================
bool Engine::EditorShell::OpenWithSystemDefault(const std::filesystem::path& file) {

	std::error_code ec{};
	if (file.empty() || !std::filesystem::exists(file, ec) || ec) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[EditorShell] target file does not exist: {}", file.string());
		return false;
	}

	const std::wstring fileW = Widen(file.string());
	const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", fileW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) > 32) {
		return true;
	}

	Logger::Output(LogType::Engine, spdlog::level::warn,
		"[EditorShell] system default open failed for: {}", file.string());
	return false;
}
