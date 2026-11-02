#include "VisualStudioLauncher.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

// c++
#include <algorithm>
#include <system_error>

// windows
#include <windows.h>
#include <shellapi.h>

// json
#include <json.hpp>

#pragma comment(lib, "shell32.lib")

namespace {

	// Installerの出力を制限時間内に取得する
	std::string ReadVisualStudioInstallations(const std::filesystem::path& finder) {

		SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
		HANDLE reader = nullptr;
		HANDLE writer = nullptr;
		if (!::CreatePipe(&reader, &writer, &attributes, 0)) {
			return {};
		}
		Engine::ScopedCleanup pipeCleanup([&]() noexcept {
			::CloseHandle(reader);
			if (writer) ::CloseHandle(writer);
		});
		if (!::SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0)) {
			return {};
		}
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = writer;
		startup.hStdError = writer;
		startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
		PROCESS_INFORMATION process{};
		std::wstring command = L"\"" + finder.wstring() + L"\" -prerelease -products * -version \"[18.0,)\" -sort -format json -utf8";
		if (!::CreateProcessW(finder.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
			return {};
		}
		::CloseHandle(writer);
		writer = nullptr;
		Engine::ScopedCleanup processCleanup([&]() noexcept {
			if (::WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
				::TerminateProcess(process.hProcess, 1);
				::WaitForSingleObject(process.hProcess, 1000);
			}
			::CloseHandle(process.hThread);
			::CloseHandle(process.hProcess);
		});
		std::string output;
		const ULONGLONG started = ::GetTickCount64();
		for (;;) {
			if (::GetTickCount64() - started >= 10000 || output.size() > 1024 * 1024) {
				return {};
			}
			DWORD available = 0;
			if (!::PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr)) {
				if (::GetLastError() != ERROR_BROKEN_PIPE) return {};
				break;
			}
			if (available != 0) {
				char buffer[4096];
				DWORD read = 0;
				if (!::ReadFile(reader, buffer, (std::min)(available, static_cast<DWORD>(sizeof(buffer))), &read, nullptr)) return {};
				output.append(buffer, read);
				continue;
			}
			if (::WaitForSingleObject(process.hProcess, 20) == WAIT_OBJECT_0) break;
		}
		DWORD exitCode = 1;
		if (::WaitForSingleObject(process.hProcess, 1000) != WAIT_OBJECT_0) return {};
		return ::GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == 0 ? output : std::string{};
	}

	// 正式版を優先し、Insidersや独自のインストール先も対象にする
	std::filesystem::path FindVisualStudioExecutable() {

		const std::filesystem::path roots[] = {
			Engine::Algorithm::GetEnvironmentPath(L"ProgramFiles"),
			Engine::Algorithm::GetEnvironmentPath(L"ProgramFiles(x86)"),
		};
		for (const auto& root : roots) {
			if (root.empty()) {
				continue;
			}
			const auto finder = root / "Microsoft Visual Studio/Installer/vswhere.exe";
			std::error_code ec;
			if (!std::filesystem::is_regular_file(finder, ec)) continue;
			const auto instances = nlohmann::json::parse(ReadVisualStudioInstallations(finder), nullptr, false);
			if (!instances.is_array()) continue;
			for (bool prerelease : {false, true}) {
				for (const auto& instance : instances) {
					if (!instance.is_object() || instance.value("isPrerelease", false) != prerelease) continue;
					const auto executable = Engine::Algorithm::PathFromUTF8(instance.value("productPath", std::string{}));
					if (executable.filename() == L"devenv.exe" && std::filesystem::is_regular_file(executable, ec)) {
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
