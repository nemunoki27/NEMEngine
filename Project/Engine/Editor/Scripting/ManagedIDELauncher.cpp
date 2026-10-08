#include "ManagedIDELauncher.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/UTFConversion.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <fstream>
#include <system_error>

// windows
#include <windows.h>
#include <shellapi.h>

// json
#include <json.hpp>

#pragma comment(lib, "shell32.lib")

namespace {

	Engine::ManagedIDESettings g_settings;
	bool g_loaded = false;

	// Project共通のScript設定を取得
	std::filesystem::path ProjectSettingsPath() {
		return Engine::RuntimePaths::GetProjectSettingsPath("ManagedScripting.json");
	}

	// 使用者ごとのIDE設定を取得
	std::filesystem::path UserSettingsPath() {
		return Engine::RuntimePaths::GetUserSettingsPath("Editor/ManagedIDE.json");
	}

	// インストール済みVisual Studioのdevenv.exeを探す
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

					const std::filesystem::path devenv =
						root / "Microsoft Visual Studio" / version / edition / "Common7/IDE/devenv.exe";
					std::error_code ec{};
					if (std::filesystem::exists(devenv, ec) && !ec) {
						return devenv;
					}
				}
			}
		}
		return {};
	}

	// 設定されたScriptプロジェクトを解決
	std::filesystem::path ProjectPath() {
		// ゲームの配置先を基準に解決
		return (Engine::RuntimePaths::GetGameRoot() / g_settings.project).lexically_normal();
	}

	// 起動引数へファイルと行位置を展開
	std::string ExpandArguments(const std::string& templ, const std::filesystem::path& file, int line, int column) {

		const auto quoteIfNeeded = [](const std::string& value) -> std::string {
			if (value.find(' ') != std::string::npos || value.find('\t') != std::string::npos) {
				return "\"" + value + "\"";
			}
			return value;
		};

		std::string result;
		result.reserve(templ.size() + 64);
		for (size_t i = 0; i < templ.size();) {
			if (templ[i] == '{') {
				if (templ.compare(i, 6, "{file}") == 0) {
					result += quoteIfNeeded(Engine::Algorithm::PathToUTF8(file));
					i += 6;
					continue;
				}
				if (templ.compare(i, 6, "{line}") == 0) {
					result += std::to_string(line);
					i += 6;
					continue;
				}
				if (templ.compare(i, 8, "{column}") == 0) {
					result += std::to_string(column);
					i += 8;
					continue;
				}
				if (templ.compare(i, 9, "{project}") == 0) {
					result += quoteIfNeeded(Engine::Algorithm::PathToUTF8(ProjectPath()));
					i += 9;
					continue;
				}
			}
			result += templ[i];
			++i;
		}
		return result;
	}

	// IDEの起動失敗をログへ記録
	void ReportLaunchDiagnostic(const std::string& message) {

		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn, "Managed IDE起動: {}", message);
	}
}

void Engine::ManagedIDELauncher::ReloadSettings() {

	g_loaded = true;
	g_settings = ManagedIDESettings{}; // 既定値へ戻してから上書き

	const std::filesystem::path projectPath = ProjectSettingsPath();
	const std::filesystem::path userPath = UserSettingsPath();
	std::error_code ec{};
	const auto load = [&](const std::filesystem::path& path, bool projectSettings) {

		if (!std::filesystem::exists(path, ec)) {
			return;
		}
		std::ifstream file(path);
		const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
		if (!root.is_object()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"Managed IDE起動: 設定を解析できないため既定値を使用します path={}", Engine::Algorithm::PathToUTF8(path));
			return;
		}
		if (projectSettings) {
			g_settings.project = root.value("project", g_settings.project);
			return;
		}
		g_settings.mode = root.value("mode", g_settings.mode);
		g_settings.executable = root.value("executable", g_settings.executable);
		g_settings.arguments = root.value("arguments", g_settings.arguments);
	};
	load(projectPath, true);
	load(userPath, false);
}

const Engine::ManagedIDESettings& Engine::ManagedIDELauncher::GetSettings() {

	if (!g_loaded) {
		ReloadSettings();
	}
	return g_settings;
}

bool Engine::ManagedIDELauncher::OpenFile(const std::filesystem::path& file, int32_t line, int32_t column) {

	if (!g_loaded) {
		ReloadSettings();
	}

	std::error_code ec{};
	if (file.empty() || !std::filesystem::exists(file, ec)) {
		ReportLaunchDiagnostic("target file does not exist: " + Engine::Algorithm::PathToUTF8(file));
		return false;
	}

	// VisualStudioが見つかれば既存のウィンドウで開く
	if (g_settings.mode == "VisualStudio") {

		const std::filesystem::path devenv = FindVisualStudioExecutable();
		if (!devenv.empty()) {
			// 既存のインスタンスへファイルを渡す
			const std::wstring parameters = L"/Edit \"" + file.wstring() + L"\"";
			const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", devenv.wstring().c_str(),
				parameters.c_str(), nullptr, SW_SHOWNORMAL);
			if (reinterpret_cast<INT_PTR>(result) > 32) {
				return true;
			}
			ReportLaunchDiagnostic("failed to launch Visual Studio (devenv). falling back to system default open.");
		}
		else {
			ReportLaunchDiagnostic("Visual Studio (devenv.exe) not found. falling back to system default open.");
		}
		// 見つからない場合はOSの関連付けを使用
	}
	else if (g_settings.mode == "Executable") {

		if (g_settings.executable.empty()) {
			ReportLaunchDiagnostic("Executable mode but no executable configured.");
			return false;
		}
		// 引数で使用するプロジェクトの存在を確認
		if (g_settings.arguments.find("{project}") != std::string::npos) {
			if (!std::filesystem::exists(ProjectPath(), ec)) {
				ReportLaunchDiagnostic("configured project not found: " + Engine::Algorithm::PathToUTF8(ProjectPath()));
				// 診断を残してファイルを開く
			}
		}
		const std::string args = ExpandArguments(g_settings.arguments, file, line, column);
		const std::wstring exeW = Engine::Algorithm::ConvertString(g_settings.executable);
		const std::wstring argsW = Engine::Algorithm::ConvertString(args);
		const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", exeW.c_str(),
			argsW.c_str(), nullptr, SW_SHOWNORMAL);
		if (reinterpret_cast<INT_PTR>(result) > 32) {
			return true;
		}
		// 起動失敗時はOSの関連付けを使用
		ReportLaunchDiagnostic("failed to launch configured executable. falling back to system default open.");
	}

	// OSの関連付けでファイルを開く
	const std::wstring fileW = file.wstring();
	const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", fileW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) > 32) {
		return true;
	}
	ReportLaunchDiagnostic("system default open failed for: " + Engine::Algorithm::PathToUTF8(file));
	return false;
}
