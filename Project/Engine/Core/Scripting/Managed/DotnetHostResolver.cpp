#include "DotnetHostResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cstdio>
#include <vector>

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>
#include <windows.h>

namespace {

	// hostfxrのパス取得に必要なサイズの通知値
	constexpr int32_t kHostAPIBufferTooSmall = 0x80008098;

	// nethostの動的なパス探索入口
	using GetHostfxrPathFn = int(NETHOST_CALLTYPE*)(char_t*, size_t*, const get_hostfxr_parameters*);

	// 診断用の実行Architecture名
	const char* ProcessArchitecture() {
#if defined(_M_ARM64)
		return "arm64";
#elif defined(_M_X64)
		return "x64";
#elif defined(_M_IX86)
		return "x86";
#else
		return "unknown";
#endif
	}

	// 戻り値を16進の診断文字列へ変換
	std::string ToHex(int32_t code) {
		char buffer[16]{};
		std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<uint32_t>(code));
		return buffer;
	}

	// hostfxrの診断をEngineログへ転送
	void HOSTFXR_CALLTYPE ForwardHostfxrError(const char_t* message) {
		if (message) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: hostfxr診断: {}", Engine::Algorithm::ConvertString(std::wstring(message)));
		}
	}

	// 実行ファイルの配置Directoryを取得
	std::filesystem::path GetExecutableDirectory() {

		return Engine::Algorithm::GetExecutablePath().parent_path();
	}

	// DLL配置先と既定検索先から依存DLLを読み込む
	HMODULE LoadLibraryFromAbsolutePath(const std::filesystem::path& absolutePath) {
		return ::LoadLibraryExW(absolutePath.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	}

	// EXE隣のnethost配置を解決
	std::filesystem::path ResolveNethostPath() {

		const std::filesystem::path executableDirectory = GetExecutableDirectory();
		if (executableDirectory.empty()) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: nethost.dllを探索する実行ファイルDirectoryを取得できません");
			return {};
		}
		return (executableDirectory / L"nethost.dll").lexically_normal();
	}

	// nethostからhostfxrの配置を取得
	std::filesystem::path ResolveHostfxrPath(const std::filesystem::path& scriptCoreAssemblyPath) {

		// EXE隣のnethostを絶対パスで読み込む
		const std::filesystem::path nethostPath = ResolveNethostPath();
		if (nethostPath.empty()) {
			return {};
		}
		std::error_code existsError{};
		if (!std::filesystem::exists(nethostPath, existsError) || existsError) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: nethost.dllが実行ファイルの隣にありません path={} arch={}",
				Engine::Algorithm::PathToUTF8(nethostPath), ProcessArchitecture());
			return {};
		}

		HMODULE nethost = LoadLibraryFromAbsolutePath(nethostPath);
		if (!nethost) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: nethost.dllの読み込みに失敗しました path={} arch={}",
				Engine::Algorithm::PathToUTF8(nethostPath), ProcessArchitecture());
			return {};
		}
		// パスの取得後にnethostを解放
		struct NethostGuard {
			HMODULE handle;
			~NethostGuard() { if (handle) { ::FreeLibrary(handle); } }
		} nethostGuard{ nethost };

		auto getHostfxrPath = reinterpret_cast<GetHostfxrPathFn>(::GetProcAddress(nethost, "get_hostfxr_path"));
		if (!getHostfxrPath) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: nethost.dllにget_hostfxr_path Exportがありません");
			return {};
		}

		get_hostfxr_parameters parameters{};
		parameters.size = sizeof(parameters);
		// 同梱のhostfxrを優先して探索
		parameters.assembly_path = scriptCoreAssemblyPath.c_str();
		parameters.dotnet_root = nullptr;

		// パス取得に必要なサイズを確認
		size_t bufferSize = 0;
		int32_t result = getHostfxrPath(nullptr, &bufferSize, &parameters);
		if (result != kHostAPIBufferTooSmall) {

			// 必要サイズを取得できなければ中止
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: get_hostfxr_pathのSize取得に失敗しました code={} arch={} "
				"一致する.NET RuntimeまたはSDKを導入してください", ToHex(result), ProcessArchitecture());
			return {};
		}

		std::vector<wchar_t> buffer(bufferSize);
		result = getHostfxrPath(buffer.data(), &bufferSize, &parameters);
		if (result != 0) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: get_hostfxr_pathに失敗しました code={} arch={}",
				ToHex(result), ProcessArchitecture());
			return {};
		}
		return std::filesystem::path(buffer.data());
	}
}

//============================================================================
//	DotnetHostResolver classMethods
//============================================================================

Engine::DotnetHostResolver::~DotnetHostResolver() {
	Shutdown();
}

bool Engine::DotnetHostResolver::Initialize(const std::filesystem::path& scriptCoreAssemblyPath,
	const std::filesystem::path& runtimeConfigPath) {

	// 再初期化前に既存の呼出入口を解除
	Shutdown();

	// Runtime設定ファイルの存在を確認
	std::error_code existsError{};
	if (!std::filesystem::exists(runtimeConfigPath, existsError) || existsError) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: runtimeconfig.jsonが見つかりません path={}", Engine::Algorithm::PathToUTF8(runtimeConfigPath));
		return false;
	}

	// 使用するhostfxrを探索
	const std::filesystem::path hostfxrPath = ResolveHostfxrPath(scriptCoreAssemblyPath);
	if (hostfxrPath.empty()) {
		// 探索側で失敗理由を記録済み
		return false;
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: hostfxrを解決しました path={} arch={} runtimeconfig={}",
		Engine::Algorithm::PathToUTF8(hostfxrPath), ProcessArchitecture(), Engine::Algorithm::PathToUTF8(runtimeConfigPath));

	// 探索結果の絶対パスからhostfxrを読み込む
	HMODULE library = LoadLibraryFromAbsolutePath(hostfxrPath);
	if (!library) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr.dllを読み込めません path={} arch={} "
			"実行HostとhostfxrのArchitectureが一致しない可能性があります Host={}",
			Engine::Algorithm::PathToUTF8(hostfxrPath), ProcessArchitecture(), ProcessArchitecture());
		return false;
	}

	// 初期化の失敗時にDLLを解放
	bool commit = false;
	struct LibraryGuard {
		HMODULE handle;
		const bool* commit;
		~LibraryGuard() { if (!*commit && handle) { ::FreeLibrary(handle); } }
	} libraryGuard{ library, &commit };

	auto initializeForRuntimeConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
		::GetProcAddress(library, "hostfxr_initialize_for_runtime_config"));
	auto getRuntimeDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
		::GetProcAddress(library, "hostfxr_get_runtime_delegate"));
	auto closeContext = reinterpret_cast<hostfxr_close_fn>(
		::GetProcAddress(library, "hostfxr_close"));
	auto setErrorWriter = reinterpret_cast<hostfxr_set_error_writer_fn>(
		::GetProcAddress(library, "hostfxr_set_error_writer"));

	if (!initializeForRuntimeConfig || !getRuntimeDelegate || !closeContext) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: 必須のhostfxr Exportが見つかりません");
		return false;
	}

	// 診断の転送先を一時的にEngineログへ変更
	hostfxr_error_writer_fn previousWriter = setErrorWriter ? setErrorWriter(&ForwardHostfxrError) : nullptr;
	struct ErrorWriterGuard {
		hostfxr_set_error_writer_fn setErrorWriter;
		hostfxr_error_writer_fn previous;
		~ErrorWriterGuard() { if (setErrorWriter) { setErrorWriter(previous); } }
	} errorWriterGuard{ setErrorWriter, previousWriter };

	// Runtime設定からHostを初期化
	hostfxr_handle context = nullptr;
	int32_t result = initializeForRuntimeConfig(runtimeConfigPath.c_str(), nullptr, &context);
	// 負の戻り値と空のContextを失敗とする
	if (result < 0 || !context) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr_initialize_for_runtime_configに失敗しました code={} runtimeconfig={}",
			ToHex(result), Engine::Algorithm::PathToUTF8(runtimeConfigPath));
		return false;
	}

	// 呼出入口の取得後に初期化Contextを閉じる
	struct ContextGuard {
		hostfxr_close_fn close;
		hostfxr_handle handle;
		~ContextGuard() { if (close && handle) { close(handle); } }
	} contextGuard{ closeContext, context };

	// Assembly読込の呼出入口を取得
	void* loadAssemblyDelegate = nullptr;
	result = getRuntimeDelegate(context, hdt_load_assembly_and_get_function_pointer, &loadAssemblyDelegate);
	if (result != 0 || !loadAssemblyDelegate) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: load_assembly_and_get_function_pointerの取得に失敗しました code={}",
			ToHex(result));
		return false;
	}

	// 初期化済みのDLLと呼出入口を公開
	library_ = library;
	loadAssemblyDelegate_ = loadAssemblyDelegate;
	commit = true;
	return true;
}

void Engine::DotnetHostResolver::Shutdown() {

	// 呼出入口を無効化してからDLLを解放
	loadAssemblyDelegate_ = nullptr;
	if (library_) {

		::FreeLibrary(static_cast<HMODULE>(library_));
		library_ = nullptr;
	}
}
