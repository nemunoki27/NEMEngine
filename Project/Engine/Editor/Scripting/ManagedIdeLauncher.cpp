#include "ManagedIdeLauncher.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

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

	Engine::ManagedIdeSettings g_settings;
	bool g_loaded = false;

	std::wstring Widen(const std::string& text) {
		if (text.empty()) {
			return std::wstring();
		}
		const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring result(static_cast<size_t>(size), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
		return result;
	}

	std::filesystem::path SettingsPath() {
		return Engine::RuntimePaths::GetGameRoot() / "ProjectSettings" / "ManagedScriptingEditor.json";
	}

	// 環境変数からpathを取得する、Program Filesの探索に使う
	std::filesystem::path GetEnvironmentPath(const char* name) {

		char* value = nullptr;
		size_t valueLength = 0;
		if (_dupenv_s(&value, &valueLength, name) != 0 || value == nullptr) {
			return {};
		}
		std::filesystem::path result = value;
		std::free(value);
		return result;
	}

	// インストール済みVisual Studioのdevenv.exeを探す、従来ProjectPanelと同じ探索順
	std::filesystem::path FindVisualStudioExecutable() {

		const std::filesystem::path roots[] = {
			GetEnvironmentPath("ProgramFiles"),
			GetEnvironmentPath("ProgramFiles(x86)"),
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

	std::filesystem::path ProjectPath() {
		// {project}はGameRoot基準の相対pathとして解決する
		return (Engine::RuntimePaths::GetGameRoot() / g_settings.project).lexically_normal();
	}

	// argumentsテンプレート中のtokenをcontrolledに展開する
	// 値に空白が含まれるpathは二重引用符で囲み、token以外の波括弧はそのまま残す
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
					result += quoteIfNeeded(file.string());
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
					result += quoteIfNeeded(ProjectPath().string());
					i += 9;
					continue;
				}
			}
			result += templ[i];
			++i;
		}
		return result;
	}

	// IDE起動失敗をlogに残す
	// 診断storeのIngestはMSBuild診断行parse専用のためIDE起動失敗はここではLoggerのみに出す
	void ReportLaunchDiagnostic(const std::string& message) {

		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn, "ManagedIdeLauncher: {}", message);
	}
}

void Engine::ManagedIdeLauncher::ReloadSettings() {

	g_loaded = true;
	g_settings = ManagedIdeSettings{}; // 既定値へ戻してから上書き

	const std::filesystem::path path = SettingsPath();
	std::error_code ec{};
	if (!std::filesystem::exists(path, ec)) {
		return; // 設定ファイルが無ければ既定値
	}
	std::ifstream file(path);
	if (!file.is_open()) {
		return;
	}
	nlohmann::json root;
	try {
		file >> root;
	}
	catch (const std::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedIdeLauncher: failed to parse {} ({}). using defaults.", path.string(), e.what());
		return;
	}
	if (!root.is_object()) {
		return;
	}
	g_settings.mode = root.value("mode", g_settings.mode);
	g_settings.executable = root.value("executable", g_settings.executable);
	g_settings.arguments = root.value("arguments", g_settings.arguments);
	g_settings.project = root.value("project", g_settings.project);
}

const Engine::ManagedIdeSettings& Engine::ManagedIdeLauncher::GetSettings() {

	if (!g_loaded) {
		ReloadSettings();
	}
	return g_settings;
}

bool Engine::ManagedIdeLauncher::OpenFile(const std::filesystem::path& file, int32_t line, int32_t column) {

	if (!g_loaded) {
		ReloadSettings();
	}

	std::error_code ec{};
	if (file.empty() || !std::filesystem::exists(file, ec)) {
		ReportLaunchDiagnostic("target file does not exist: " + file.string());
		return false;
	}

	// VisualStudioモードは既定、devenv.exeを探して/Editで開き、見つからなければsystem defaultへ
	if (g_settings.mode == "VisualStudio") {

		const std::filesystem::path devenv = FindVisualStudioExecutable();
		if (!devenv.empty()) {
			// /Editは既存インスタンスがあればそれでfileを開く、従来ProjectPanelと同じ
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
		// 見つからない場合は下のsystem default経路へ落ちる
	}
	else if (g_settings.mode == "Executable") {

		if (g_settings.executable.empty()) {
			ReportLaunchDiagnostic("Executable mode but no executable configured.");
			return false;
		}
		// {project}を使う設定ならprojectの存在も検証する
		if (g_settings.arguments.find("{project}") != std::string::npos) {
			if (!std::filesystem::exists(ProjectPath(), ec)) {
				ReportLaunchDiagnostic("configured project not found: " + ProjectPath().string());
				// project無しでもfileは開けるようfallbackへ進みdiagnosticは残す
			}
		}
		const std::string args = ExpandArguments(g_settings.arguments, file, line, column);
		const std::wstring exeW = Widen(g_settings.executable);
		const std::wstring argsW = Widen(args);
		const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", exeW.c_str(),
			argsW.c_str(), nullptr, SW_SHOWNORMAL);
		if (reinterpret_cast<INT_PTR>(result) > 32) {
			return true;
		}
		// 起動失敗時はfileのみのfallbackへ
		ReportLaunchDiagnostic("failed to launch configured executable. falling back to system default open.");
	}

	// SystemDefaultもしくは上の失敗時のfallback、OS既定の関連付けでfileを開きlineとcolumnは無視する
	const std::wstring fileW = Widen(file.string());
	const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", fileW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) > 32) {
		return true;
	}
	ReportLaunchDiagnostic("system default open failed for: " + file.string());
	return false;
}
