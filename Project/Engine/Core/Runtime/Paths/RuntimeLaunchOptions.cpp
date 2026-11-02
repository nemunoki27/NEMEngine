#include "RuntimePathResolution.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <stdexcept>
#include <cwchar>
#include <string_view>
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

Engine::RuntimePathDetail::RuntimeLaunchOptions Engine::RuntimePathDetail::ReadLaunchOptions() {

	RuntimeLaunchOptions result;
	int count = 0;
	wchar_t** arguments = ::CommandLineToArgvW(::GetCommandLineW(), &count);
	if (!arguments) { throw std::runtime_error("起動引数を取得できません"); }
	try {
		// 他のツールの引数はそのまま残す
		for (int index = 1; index < count; ++index) {
			const std::wstring_view name(arguments[index]);
			std::filesystem::path* destination = nullptr;
			if (name == L"--project") { destination = &result.project; }
			else if (name == L"--engine-root") { destination = &result.engineRoot; }
			else if (name == L"--toolchain-root") { destination = &result.toolchainRoot; }
			if (!destination) { continue; }
			if (!destination->empty() || index + 1 == count) { throw std::runtime_error("配置の起動引数が不正です"); }
			*destination = NormalizePath(arguments[++index]);
		}
	} catch (...) {
		::LocalFree(arguments);
		throw;
	}
	::LocalFree(arguments);
	return result;
}

void Engine::RuntimePathDetail::ResolveInstalledEngine(RuntimePaths::PathState& state, const RuntimeLaunchOptions& options) {

	state.toolchainRoot = options.toolchainRoot.empty() ? GetEnvironmentPath(L"NEMENGINE_TOOLCHAIN_ROOT") : options.toolchainRoot;
	const auto manifestPath = state.engineProjectRoot / "engine.json";
	if (!std::filesystem::is_regular_file(manifestPath)) {
		if (!options.engineRoot.empty()) { throw std::runtime_error("エンジンの配布情報がありません"); }
		// 製品には実行用の.NETだけを同梱する
		const auto bundled = GetExecutablePath().parent_path() / "DotNet";
		if (ExistsDirectory(bundled)) { state.toolchainRoot = bundled; }
		return;
	}
	const auto manifest = JsonFile::Load(manifestPath, false);
	const auto project = JsonFile::Load(state.projectDescriptorPath, false);
	state.engineVersion = manifest.value("version", std::string{});
	if (manifest.value("schemaVersion", 0) != 1 || manifest.value("platform", std::string{}) != "win-x64" ||
		state.engineVersion.empty() || project.value("engineVersion", std::string{}) != state.engineVersion) {
		throw std::runtime_error("プロジェクトとエンジンの使用版が一致しません ランチャーから起動してください");
	}
	if (state.toolchainRoot.empty() || !std::filesystem::is_regular_file(state.toolchainRoot / "dotnet.exe")) {
		throw std::runtime_error("C#ツールチェーンがありません ランチャーで修復してください");
	}
	state.toolchainRoot = NormalizePath(state.toolchainRoot);
	state.installedEngine = true;
}

void Engine::RuntimePathDetail::AcquireInstalledEngineLeases(RuntimePaths::PathState& state) {

	if (!state.installedEngine) { return; }
	auto acquire = [](const std::filesystem::path& path, DWORD access, DWORD creation) {
		HANDLE handle = ::CreateFileW(path.c_str(), access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) { throw std::runtime_error("エンジンまたはプロジェクトが使用中です"); }
		return std::shared_ptr<void>(handle, [](void* value) { ::CloseHandle(value); });
	};
	const auto leasePath = [](const std::filesystem::path& root, const wchar_t* fallback) {
		const auto lease = root.parent_path() / (L"." + root.filename().wstring() + L".lease");
		return std::filesystem::is_regular_file(lease) ? lease : root / fallback;
	};
	if (!state.engineLease) { state.engineLease = acquire(leasePath(state.engineProjectRoot, L"engine.json"), GENERIC_READ, OPEN_EXISTING); }
	if (!state.toolchainLease) { state.toolchainLease = acquire(leasePath(state.toolchainRoot, L"dotnet.exe"), GENERIC_READ, OPEN_EXISTING); }
	if (state.projectLease || ::_wcsicmp(GetExecutablePath().stem().c_str(), L"NEMEditor") != 0) { return; }
	state.projectLease = acquire(state.libraryRoot / "EditorSession.lock", GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS);
	// 終了後に残ったファイルは次回の起動で再利用する
	const std::string process = std::to_string(::GetCurrentProcessId());
	DWORD written = 0;
	::SetEndOfFile(state.projectLease.get());
	if (!::WriteFile(state.projectLease.get(), process.data(), static_cast<DWORD>(process.size()), &written, nullptr) || written != process.size()) {
		throw std::runtime_error("Editorの起動状態を記録できません");
	}
}
