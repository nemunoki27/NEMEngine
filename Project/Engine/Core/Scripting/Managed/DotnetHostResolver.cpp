#include "DotnetHostResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// .NET公式native hosting
#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

// windows
#include <windows.h>
// c++
#include <cstdio>
#include <vector>

namespace {

	// get_hostfxr_pathがバッファ不足を示す戻り値、詳細はnethost.hのRemarks参照
	constexpr int32_t kHostApiBufferTooSmall = 0x80008098;

	// nethost.dllのget_hostfxr_pathシグネチャで動的ロード用
	using GetHostfxrPathFn = int(NETHOST_CALLTYPE*)(char_t*, size_t*, const get_hostfxr_parameters*);

	// ビルド対象のプロセスアーキテクチャ名で診断用
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

	std::string ToUtf8(const std::wstring& text) {
		return Engine::Algorithm::ConvertString(text);
	}

	std::string ToUtf8Path(const std::filesystem::path& path) {
		return ToUtf8(path.wstring());
	}

	// 戻り値コードを符号なし16進で表示して負のエラーコードを読みやすくする
	std::string ToHex(int32_t code) {
		char buffer[16]{};
		std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<uint32_t>(code));
		return buffer;
	}

	// hostfxrの詳細エラーメッセージをエンジンログへ転送する、既定はstderr
	void HOSTFXR_CALLTYPE ForwardHostfxrError(const char_t* message) {
		if (message) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: hostfxr診断: {}", ToUtf8(message));
		}
	}

	// 現在実行中のexeのディレクトリを取得する、固定長で切り詰めず必要なら動的に拡張する
	std::filesystem::path GetExecutableDirectory() {

		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;) {

			const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0) {
				return {};
			}
			// lengthがバッファ未満なら切り詰められていないので確定
			if (length < buffer.size()) {
				return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
			}
			// 切り詰めなのでサニティ上限を設けつつバッファを倍化して再試行する
			if (buffer.size() >= (1u << 20)) {
				return {};
			}
			buffer.resize(buffer.size() * 2);
		}
	}

	// 絶対パスのDLLを安全な検索フラグでロードし依存DLLを自身のディレクトリとsystem32等の既定検索から探す、CWDやPATH依存のbare-name探索を避ける
	HMODULE LoadLibraryFromAbsolutePath(const std::filesystem::path& absolutePath) {
		return ::LoadLibraryExW(absolutePath.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	}

	// exeディレクトリ直下に配置されたnethost.dllの絶対パスを解決する
	std::filesystem::path ResolveNethostPath() {

		const std::filesystem::path executableDirectory = GetExecutableDirectory();
		if (executableDirectory.empty()) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: nethost.dllを探索する実行ファイルDirectoryを取得できません");
			return {};
		}
		return (executableDirectory / L"nethost.dll").lexically_normal();
	}

	// nethostのget_hostfxr_pathでhostfxrの絶対パスを解決する、nethost.dllを動的ロードしバッファは必要量を問い合わせてから確保し直す
	std::filesystem::path ResolveHostfxrPath(const std::filesystem::path& scriptCoreAssemblyPath) {

		// nethost.dllはexeディレクトリ直下の絶対パスで明示ロードしbare nameや相対パスやCWDやPATH依存のロードはしない
		const std::filesystem::path nethostPath = ResolveNethostPath();
		if (nethostPath.empty()) {
			return {};
		}
		std::error_code existsError{};
		if (!std::filesystem::exists(nethostPath, existsError) || existsError) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: nethost.dllが実行ファイルの隣にありません path={} arch={}",
				ToUtf8Path(nethostPath), ProcessArchitecture());
			return {};
		}

		HMODULE nethost = LoadLibraryFromAbsolutePath(nethostPath);
		if (!nethost) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"ManagedScriptRuntime: nethost.dllの読み込みに失敗しました path={} arch={}",
				ToUtf8Path(nethostPath), ProcessArchitecture());
			return {};
		}
		// hostfxrパス解決後は不要なので、関数を抜けるときに必ず解放する
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
		// assembly_pathを渡すと、self-contained同梱hostfxrがあれば優先し、無ければグローバル登録を使う
		parameters.assembly_path = scriptCoreAssemblyPath.c_str();
		parameters.dotnet_root = nullptr;

		// まず必要バッファサイズを問い合わせる
		size_t bufferSize = 0;
		int32_t result = getHostfxrPath(nullptr, &bufferSize, &parameters);
		if (result != kHostApiBufferTooSmall) {

			// サイズ問い合わせ以外で失敗した場合はnethostがhostfxrを特定できない、.NET runtime未導入など
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

	// 再初期化に備えて、既存状態を必ず解放してから始める
	Shutdown();

	// runtimeconfig.jsonの存在を区別したログで検証する
	std::error_code existsError{};
	if (!std::filesystem::exists(runtimeConfigPath, existsError) || existsError) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: runtimeconfig.jsonが見つかりません path={}", ToUtf8Path(runtimeConfigPath));
		return false;
	}

	// nethostでhostfxrを解決する
	const std::filesystem::path hostfxrPath = ResolveHostfxrPath(scriptCoreAssemblyPath);
	if (hostfxrPath.empty()) {
		// 失敗理由はResolveHostfxrPath側でログ済み
		return false;
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: hostfxrを解決しました path={} arch={} runtimeconfig={}",
		ToUtf8Path(hostfxrPath), ProcessArchitecture(), ToUtf8Path(runtimeConfigPath));

	// hostfxr.dllをget_hostfxr_pathが返した絶対パスのままロードし相対化やbare-name変換はせずnethostと同じ安全な検索フラグを使う
	HMODULE library = LoadLibraryFromAbsolutePath(hostfxrPath);
	if (!library) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr.dllを読み込めません path={} arch={} "
			"実行HostとhostfxrのArchitectureが一致しない可能性があります Host={}",
			ToUtf8Path(hostfxrPath), ProcessArchitecture(), ProcessArchitecture());
		return false;
	}

	// 以降の失敗経路では必ずFreeLibraryし、成功時のみ確定して保持する
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

	// hostfxrの詳細エラーをログへ転送し、関数終了時に元のwriterへ戻す
	hostfxr_error_writer_fn previousWriter = setErrorWriter ? setErrorWriter(&ForwardHostfxrError) : nullptr;
	struct ErrorWriterGuard {
		hostfxr_set_error_writer_fn setErrorWriter;
		hostfxr_error_writer_fn previous;
		~ErrorWriterGuard() { if (setErrorWriter) { setErrorWriter(previous); } }
	} errorWriterGuard{ setErrorWriter, previousWriter };

	// runtimeconfigでホストを初期化する
	hostfxr_handle context = nullptr;
	int32_t result = initializeForRuntimeConfig(runtimeConfigPath.c_str(), nullptr, &context);
	// Success 0とSuccess_HostAlreadyInitialized 1とSuccess_DifferentRuntimeProperties 2は成功で、負値は失敗
	if (result < 0 || !context) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr_initialize_for_runtime_configに失敗しました code={} runtimeconfig={}",
			ToHex(result), ToUtf8Path(runtimeConfigPath));
		return false;
	}

	// コンテキストは成功と失敗どちらの経路でも必ず閉じる、デリゲート取得後は不要
	struct ContextGuard {
		hostfxr_close_fn close;
		hostfxr_handle handle;
		~ContextGuard() { if (close && handle) { close(handle); } }
	} contextGuard{ closeContext, context };

	// load_assembly_and_get_function_pointerデリゲートを取得する
	void* loadAssemblyDelegate = nullptr;
	result = getRuntimeDelegate(context, hdt_load_assembly_and_get_function_pointer, &loadAssemblyDelegate);
	if (result != 0 || !loadAssemblyDelegate) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: load_assembly_and_get_function_pointerの取得に失敗しました code={}",
			ToHex(result));
		return false;
	}

	// 成功したのでライブラリを保持してデリゲートを公開する
	library_ = library;
	loadAssemblyDelegate_ = loadAssemblyDelegate;
	commit = true;
	return true;
}

void Engine::DotnetHostResolver::Shutdown() {

	// 先にデリゲートを無効化してからライブラリを解放し、解放後のポインタ参照を防ぐ
	loadAssemblyDelegate_ = nullptr;
	if (library_) {

		::FreeLibrary(static_cast<HMODULE>(library_));
		library_ = nullptr;
	}
}
