#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// windows
#include <windows.h>
// c++
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <string_view>

//============================================================================
//	ManagedScriptRuntime classMethods
//============================================================================
namespace {

	// hostfxrのデリゲート種別
	constexpr int32_t kLoadAssemblyAndGetFunctionPointer = 5;

	// 現在のビルド設定名を返す
	std::string GetBuildProfile() {
		return _PROFILE;
	}

	// パスをUTF-8文字列へ変換する
	std::string ToUtf8Path(const std::filesystem::path& path) {
		return Engine::Algorithm::ConvertString(path.wstring());
	}

	// バージョン文字列を数値配列へ変換する
	std::array<int32_t, 4> ParseVersion(const std::wstring& text) {
		std::array<int32_t, 4> version{};
		size_t begin = 0;
		uint32_t index = 0;
		while (begin < text.size() && index < version.size()) {
			size_t end = text.find(L'.', begin);
			if (end == std::wstring::npos) {
				end = text.size();
			}

			int32_t value = 0;
			for (size_t i = begin; i < end; ++i) {
				if (text[i] < L'0' || L'9' < text[i]) {
					continue;
				}
				value = value * 10 + static_cast<int32_t>(text[i] - L'0');
			}
			version[index++] = value;
			begin = end + 1;
		}
		return version;
	}

	// バージョンが新しいか判定
	bool IsNewerVersion(const std::wstring& candidate, const std::wstring& current) {
		return ParseVersion(current) < ParseVersion(candidate);
	}

	// 環境変数からパスを取得
	std::filesystem::path GetEnvironmentPath(const wchar_t* name) {
		wchar_t* value = nullptr;
		size_t length = 0;
		if (_wdupenv_s(&value, &length, name) != 0 || !value) {
			return {};
		}
		std::filesystem::path result = value;
		std::free(value);
		return result;
	}

	// dotnetルートを特定
	std::filesystem::path FindDotnetRoot() {
		if (auto path = GetEnvironmentPath(L"DOTNET_ROOT_X64"); !path.empty()) {
			return path;
		}
		if (auto path = GetEnvironmentPath(L"DOTNET_ROOT"); !path.empty()) {
			return path;
		}
		return L"C:/Program Files/dotnet";
	}

	// hostfxr.dllのパスを特定
	std::filesystem::path FindHostfxrPath() {
		const std::filesystem::path fxrRoot = FindDotnetRoot() / "host/fxr";
		if (!std::filesystem::exists(fxrRoot)) {
			return {};
		}

		std::filesystem::path bestPath{};
		std::wstring bestVersion{};
		for (const auto& entry : std::filesystem::directory_iterator(fxrRoot)) {
			if (!entry.is_directory()) {
				continue;
			}
			const std::wstring version = entry.path().filename().wstring();
			if (bestVersion.empty() || IsNewerVersion(version, bestVersion)) {
				bestVersion = version;
				bestPath = entry.path() / "hostfxr.dll";
			}
		}
		return std::filesystem::exists(bestPath) ? bestPath : std::filesystem::path{};
	}

	// 候補の中から最初に見つかったパスを返す
	std::filesystem::path FindFirstExistingPath(const std::vector<std::filesystem::path>& paths) {
		for (const auto& path : paths) {
			if (std::filesystem::exists(path)) {
				return path;
			}
		}
		return {};
	}

	// ScriptCoreのアセンブリパスを解決
	std::filesystem::path ResolveScriptCoreAssemblyPath() {
		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		const std::filesystem::path engineRoot = Engine::RuntimePaths::GetEngineProjectRoot().parent_path();
		return FindFirstExistingPath({
			Engine::RuntimePaths::GetEngineLibraryRoot() / "Managed" / profile / "NEM.ScriptCore.dll",
			engineRoot / "Generated/Managed/NEM.ScriptCore" / profile / "NEM.ScriptCore.dll",
			Engine::RuntimePaths::GetGameRoot() / "Managed" / profile / "NEM.ScriptCore.dll",
			current / "Managed/NEM.ScriptCore.dll"
			});
	}

	// ゲーム側アセンブリパスを解決
	std::filesystem::path ResolveGameAssemblyPath() {
		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		return FindFirstExistingPath({
			Engine::RuntimePaths::GetGameRoot() / "Managed" / profile / "GameScripts.dll",
			current / "Managed/GameScripts.dll"
			});
	}

	// ゲームスクリプトのプロジェクトパスを解決
	std::filesystem::path ResolveGameScriptProjectPath() {
		const std::filesystem::path current = std::filesystem::current_path();
		return FindFirstExistingPath({
			current / "Scripts/GameScripts.csproj",
			Engine::RuntimePaths::GetGameRoot() / "Scripts/GameScripts.csproj"
			});
	}

	// マネージドデバッグ環境の構成（JIT最適化抑制など）
	void ConfigureManagedDebugEnvironment() {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		::SetEnvironmentVariableW(L"COMPlus_ReadyToRun", L"0");
		::SetEnvironmentVariableW(L"COMPlus_TieredCompilation", L"0");
		::SetEnvironmentVariableW(L"COMPlus_ZapDisable", L"1");
		::SetEnvironmentVariableW(L"DOTNET_EnableDiagnostics", L"1");
#endif
	}

	// スコープ内で環境変数を一時的に上書きするヘルパー
	class ScopedEnvironmentVariableOverride final {
	public:
		ScopedEnvironmentVariableOverride(const wchar_t* name, const wchar_t* value) :
			name_(name) {
			wchar_t* previous = nullptr;
			size_t previousLength = 0;
			if (_wdupenv_s(&previous, &previousLength, name_) == 0 && previous) {
				hadPreviousValue_ = true;
				previousValue_ = previous;
			}
			::SetEnvironmentVariableW(name_, value);
		}
		~ScopedEnvironmentVariableOverride() {
			::SetEnvironmentVariableW(name_, hadPreviousValue_ ? previousValue_.c_str() : nullptr);
			if (hadPreviousValue_) {
				std::free(const_cast<wchar_t*>(previousValue_.c_str()));
			}
		}
	private:
		const wchar_t* name_;
		bool hadPreviousValue_ = false;
		std::wstring previousValue_;
	};

	// パスをクォートで囲む
	std::wstring QuoteCommandPath(const std::filesystem::path& path) {
		return L"\"" + path.wstring() + L"\"";
	}

	// 文字列変換ヘルパー
	std::wstring ToWideAscii(const std::string& text) {
		return Engine::Algorithm::ConvertString(text);
	}

	// スナップショットにファイルを追加
	void TryAddSnapshotFile(std::unordered_map<std::string, std::filesystem::file_time_type>& snapshot,
		const std::filesystem::path& path) {
		if (std::filesystem::exists(path)) {
			snapshot.emplace(ToUtf8Path(path), std::filesystem::last_write_time(path));
		}
	}

	// スクリプトソースのスナップショットを収集
	void CollectScriptSnapshotFiles(std::unordered_map<std::string, std::filesystem::file_time_type>& snapshot,
		const std::filesystem::path& root) {
		if (!std::filesystem::exists(root)) {
			return;
		}
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
			if (entry.is_regular_file() && entry.path().extension() == ".cs") {
				snapshot.emplace(ToUtf8Path(entry.path()), entry.last_write_time());
			}
		}
	}

	// スナップショットが変化したか判定
	bool HasSnapshotChanged(const std::unordered_map<std::string, std::filesystem::file_time_type>& current,
		const std::unordered_map<std::string, std::filesystem::file_time_type>& previous) {
		if (current.size() != previous.size()) {
			return true;
		}
		for (const auto& [path, time] : current) {
			auto it = previous.find(path);
			if (it == previous.end() || it->second != time) {
				return true;
			}
		}
		return false;
	}

	// C#側へ渡す文字列作成
	std::string MakeString(const char* text) {
		return text ? std::string(text) : std::string{};
	}

} // namespace

bool Engine::ManagedScriptRuntime::Init() {

	if (initialized_) {
		return true;
	}

	ConfigureManagedDebugEnvironment();

	scriptCoreAssemblyPath_ = ResolveScriptCoreAssemblyPath();
	if (scriptCoreAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: NEM.ScriptCore.dll was not found.");
		return false;
	}

	if (!LoadHostfxr()) {
		return false;
	}

	if (!LoadBridgeFunctions()) {
		return false;
	}

	// ネイティブ側API（C++側の機能をC#から呼ぶための関数群）を初期化
	ManagedNativeApiTable callbacks{};
	callbacks.log = &ManagedScriptRuntime::LogCallback;
	callbacks.getDeltaTime = &ManagedScriptRuntime::GetDeltaTimeCallback;
	callbacks.getFixedDeltaTime = &ManagedScriptRuntime::GetFixedDeltaTimeCallback;
	callbacks.getKey = &ManagedScriptRuntime::GetKeyCallback;
	callbacks.getKeyDown = &ManagedScriptRuntime::GetKeyDownCallback;
	callbacks.getKeyUp = &ManagedScriptRuntime::GetKeyUpCallback;
	callbacks.getMouseButton = &ManagedScriptRuntime::GetMouseButtonCallback;
	callbacks.getMouseButtonDown = &ManagedScriptRuntime::GetMouseButtonDownCallback;
	callbacks.getMouseButtonUp = &ManagedScriptRuntime::GetMouseButtonUpCallback;
	callbacks.getMousePosition = &ManagedScriptRuntime::GetMousePositionCallback;
	callbacks.getMouseDelta = &ManagedScriptRuntime::GetMouseDeltaCallback;
	callbacks.getMouseWheel = &ManagedScriptRuntime::GetMouseWheelCallback;
	callbacks.getGamepadButton = &ManagedScriptRuntime::GetGamepadButtonCallback;
	callbacks.getGamepadButtonDown = &ManagedScriptRuntime::GetGamepadButtonDownCallback;
	callbacks.isGamepadConnected = &ManagedScriptRuntime::IsGamepadConnectedCallback;
	callbacks.getLeftStick = &ManagedScriptRuntime::GetLeftStickCallback;
	callbacks.getRightStick = &ManagedScriptRuntime::GetRightStickCallback;
	callbacks.getLeftTrigger = &ManagedScriptRuntime::GetLeftTriggerCallback;
	callbacks.getRightTrigger = &ManagedScriptRuntime::GetRightTriggerCallback;
	callbacks.isAlive = &ManagedScriptRuntime::IsAliveCallback;
	callbacks.copyName = &ManagedScriptRuntime::CopyNameCallback;
	callbacks.setName = &ManagedScriptRuntime::SetNameCallback;
	callbacks.getActiveSelf = &ManagedScriptRuntime::GetActiveSelfCallback;
	callbacks.setActiveSelf = &ManagedScriptRuntime::SetActiveSelfCallback;
	callbacks.getActiveInHierarchy = &ManagedScriptRuntime::GetActiveInHierarchyCallback;
	callbacks.getParent = &ManagedScriptRuntime::GetParentCallback;
	callbacks.getFirstChild = &ManagedScriptRuntime::GetFirstChildCallback;
	callbacks.getNextSibling = &ManagedScriptRuntime::GetNextSiblingCallback;
	callbacks.setParent = &ManagedScriptRuntime::SetParentCallback;
	callbacks.getPosition = &ManagedScriptRuntime::GetPositionCallback;
	callbacks.setPosition = &ManagedScriptRuntime::SetPositionCallback;
	callbacks.getLocalPosition = &ManagedScriptRuntime::GetLocalPositionCallback;
	callbacks.setLocalPosition = &ManagedScriptRuntime::SetLocalPositionCallback;
	callbacks.getLocalScale = &ManagedScriptRuntime::GetLocalScaleCallback;
	callbacks.setLocalScale = &ManagedScriptRuntime::SetLocalScaleCallback;
	callbacks.getLocalRotation = &ManagedScriptRuntime::GetLocalRotationCallback;
	callbacks.setLocalRotation = &ManagedScriptRuntime::SetLocalRotationCallback;

	if (!initializeNativeApi_ || initializeNativeApi_(&callbacks) == 0) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to initialize native callbacks.");
		Finalize();
		return false;
	}

	initialized_ = true;
	scriptSourceSnapshot_.clear();
	hasScriptSourceSnapshot_ = false;
	nextScriptSourceScanTime_ = std::chrono::steady_clock::time_point{};

	// 初期アセンブリをロード
	if (!ReloadGameAssembly()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dll was not loaded. Managed scripts will be unavailable.");
	}
	return true;
}

void Engine::ManagedScriptRuntime::Finalize() {

	UnloadGameAssembly();
	fieldCache_.clear();
	scriptSourceSnapshot_.clear();
	hasScriptSourceSnapshot_ = false;
	nextScriptSourceScanTime_ = std::chrono::steady_clock::time_point{};
	currentContext_ = nullptr;
	initialized_ = false;

	// 関数ポインタのリセット
	initializeNativeApi_ = nullptr;
	loadGameAssembly_ = nullptr;
	unloadGameAssembly_ = nullptr;
	getScriptTypeCount_ = nullptr;
	copyScriptTypeName_ = nullptr;
	getSerializedFieldCount_ = nullptr;
	copySerializedFieldInfo_ = nullptr;
	createInstance_ = nullptr;
	setSerializedFields_ = nullptr;
	destroyInstance_ = nullptr;
	invokeAwake_ = nullptr;
	invokeStart_ = nullptr;
	invokeOnEnable_ = nullptr;
	invokeOnDisable_ = nullptr;
	invokeOnDestroy_ = nullptr;
	invokeFixedUpdate_ = nullptr;
	invokeUpdate_ = nullptr;
	invokeLateUpdate_ = nullptr;
	invokeCollisionEnter_ = nullptr;
	invokeCollisionStay_ = nullptr;
	invokeCollisionExit_ = nullptr;

	ReleaseHostfxr();
}

void Engine::ManagedScriptRuntime::RefreshScriptTypes() {

	BehaviorTypeRegistry::GetInstance().ClearManaged();
	fieldCache_.clear();

	if (!initialized_ || !getScriptTypeCount_ || !copyScriptTypeName_) {
		return;
	}

	const int32_t typeCount = getScriptTypeCount_();
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: managed script type count={}", typeCount);
	for (int32_t i = 0; i < typeCount; ++i) {

		char name[256]{};
		if (copyScriptTypeName_(i, name, static_cast<int32_t>(sizeof(name))) <= 0) {
			continue;
		}
		BehaviorTypeRegistry::GetInstance().RegisterManaged(name);
		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptRuntime: registered managed script type={}", name);
	}
}

bool Engine::ManagedScriptRuntime::BuildGameAssembly() {

	const std::filesystem::path projectPath = ResolveGameScriptProjectPath();
	if (projectPath.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptRuntime: GameScripts.csproj was not found. Skipping C# script build.");
		return true;
	}

	const std::wstring command =
		L"set DOTNET_CLI_UI_LANGUAGE=en && dotnet build " + QuoteCommandPath(projectPath) +
		L" -c \"" + ToWideAscii(GetBuildProfile()) +
		L"\" --nologo --no-dependencies -p:DebugType=portable -p:DebugSymbols=true -p:Optimize=false";

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: building GameScripts.csproj path={}", ToUtf8Path(projectPath));

	const int result = _wsystem(command.c_str());
	if (result != 0) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: dotnet build failed. code={}", result);
		return false;
	}

	gameAssemblyPath_ = ResolveGameAssemblyPath();
	return true;
}

bool Engine::ManagedScriptRuntime::ReloadGameAssembly(bool waitForManagedDebugger) {

	if (!initialized_) {
		return false;
	}

	auto doReload = [this]() {
		UnloadGameAssembly();
		gameAssemblyPath_ = ResolveGameAssemblyPath();
		if (!LoadGameAssembly()) {
			return false;
		}
		RefreshScriptTypes();
		return true;
	};

	if (waitForManagedDebugger) {
		ScopedEnvironmentVariableOverride waitOverride(L"NEM_MANAGED_WAIT_FOR_DEBUGGER", L"1");
		return doReload();
	}
	return doReload();
}

void Engine::ManagedScriptRuntime::UnloadGameAssembly() {

	fieldCache_.clear();
	BehaviorTypeRegistry::GetInstance().ClearManaged();

	if (unloadGameAssembly_) {
		unloadGameAssembly_();
	}
}

void Engine::ManagedScriptRuntime::AutoRebuildOnScriptChanges() {

	if (!initialized_) {
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	if (now < nextScriptSourceScanTime_) {
		return;
	}
	nextScriptSourceScanTime_ = now + std::chrono::milliseconds(500);

	const std::filesystem::path projectPath = ResolveGameScriptProjectPath();
	if (projectPath.empty()) {
		scriptSourceSnapshot_.clear();
		hasScriptSourceSnapshot_ = false;
		return;
	}

	std::unordered_map<std::string, std::filesystem::file_time_type> currentSnapshot{};
	TryAddSnapshotFile(currentSnapshot, projectPath);

	const std::filesystem::path scriptsRoot = projectPath.parent_path();
	const std::filesystem::path gameAssetsRoot = scriptsRoot.parent_path() / "GameAssets";
	CollectScriptSnapshotFiles(currentSnapshot, scriptsRoot);
	CollectScriptSnapshotFiles(currentSnapshot, gameAssetsRoot);

	if (!hasScriptSourceSnapshot_) {
		scriptSourceSnapshot_ = std::move(currentSnapshot);
		hasScriptSourceSnapshot_ = true;
		return;
	}

	if (!HasSnapshotChanged(currentSnapshot, scriptSourceSnapshot_)) {
		return;
	}

	scriptSourceSnapshot_ = std::move(currentSnapshot);

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: detected C# source changes. rebuilding GameScripts...");
	UnloadGameAssembly();
	if (!BuildGameAssembly()) {
		// ビルド失敗時は直前のDLLを再ロードして、Inspector上のスクリプト情報を維持する
		ReloadGameAssembly();
		return;
	}
	if (!ReloadGameAssembly()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: source change was detected, but GameScripts.dll reload failed.");
	}
}

int32_t Engine::ManagedScriptRuntime::CreateInstance(const std::string& typeName,
	ECSWorld& world, const Entity& entity, const nlohmann::json& serializedFields) {

	if (!initialized_ || !createInstance_) {
		return 0;
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	return createInstance_(typeName.c_str(), MakeNativeEntity(world, entity), json.c_str());
}

void Engine::ManagedScriptRuntime::SetSerializedFields(int32_t handle, const nlohmann::json& serializedFields) {

	if (!initialized_ || !setSerializedFields_ || handle == 0) {
		return;
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	setSerializedFields_(handle, json.c_str());
}

void Engine::ManagedScriptRuntime::DestroyInstance(int32_t handle) {

	if (!initialized_ || !destroyInstance_ || handle == 0) {
		return;
	}
	destroyInstance_(handle);
}

void Engine::ManagedScriptRuntime::InvokeAwake(int32_t handle, const SystemContext& context) {
	Invoke(invokeAwake_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeStart(int32_t handle, const SystemContext& context) {
	Invoke(invokeStart_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeOnEnable(int32_t handle, const SystemContext& context) {
	Invoke(invokeOnEnable_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeOnDisable(int32_t handle, const SystemContext& context) {
	Invoke(invokeOnDisable_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeOnDestroy(int32_t handle, const SystemContext& context) {
	Invoke(invokeOnDestroy_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeFixedUpdate(int32_t handle, const SystemContext& context) {
	Invoke(invokeFixedUpdate_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeUpdate(int32_t handle, const SystemContext& context) {
	Invoke(invokeUpdate_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeLateUpdate(int32_t handle, const SystemContext& context) {
	Invoke(invokeLateUpdate_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeCollisionEnter(int32_t handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	InvokeCollision(invokeCollisionEnter_, handle, context, collision);
}

void Engine::ManagedScriptRuntime::InvokeCollisionStay(int32_t handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	InvokeCollision(invokeCollisionStay_, handle, context, collision);
}

void Engine::ManagedScriptRuntime::InvokeCollisionExit(int32_t handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	InvokeCollision(invokeCollisionExit_, handle, context, collision);
}

const std::vector<Engine::ManagedScriptField>& Engine::ManagedScriptRuntime::GetSerializedFields(const std::string& typeName) {

	static const std::vector<ManagedScriptField> kEmpty{};

	if (auto it = fieldCache_.find(typeName); it != fieldCache_.end()) {
		return it->second;
	}
	if (!initialized_ || !getSerializedFieldCount_ || !copySerializedFieldInfo_) {
		return kEmpty;
	}

	const int32_t fieldCount = getSerializedFieldCount_(typeName.c_str());
	std::vector<ManagedScriptField> fields{};
	fields.reserve(std::max(0, fieldCount));

	for (int32_t i = 0; i < fieldCount; ++i) {

		ManagedNativeSerializedFieldInfo nativeInfo{};
		if (copySerializedFieldInfo_(typeName.c_str(), i, &nativeInfo) == 0) {
			continue;
		}

		ManagedScriptField field{};
		field.name = MakeString(nativeInfo.name);
		field.displayName = MakeString(nativeInfo.displayName);
		field.kind = static_cast<ManagedSerializedFieldKind>(nativeInfo.kind);
		field.isPublic = nativeInfo.isPublic != 0;
		field.defaultValueJson = MakeString(nativeInfo.defaultValueJson);
		fields.emplace_back(std::move(field));
	}

	auto [it, inserted] = fieldCache_.emplace(typeName, std::move(fields));
	return it->second;
}

bool Engine::ManagedScriptRuntime::TryResolveScriptTypeName(const std::string_view& scriptName, std::string& outTypeName) const {

	const auto& registry = BehaviorTypeRegistry::GetInstance();
	if (const BehaviorTypeInfo* info = registry.FindByName(scriptName)) {
		if (info->managed) {
			outTypeName = info->name;
			return true;
		}
	}

	const std::string simpleName = MakeSimpleTypeName(scriptName);
	if (const BehaviorTypeInfo* info = registry.FindManagedBySimpleName(simpleName)) {
		outTypeName = info->name;
		return true;
	}
	return false;
}

Engine::ManagedScriptRuntime& Engine::ManagedScriptRuntime::GetInstance() {
	static ManagedScriptRuntime runtime;
	return runtime;
}

bool Engine::ManagedScriptRuntime::LoadHostfxr() {

	const std::filesystem::path hostfxrPath = FindHostfxrPath();
	if (hostfxrPath.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr.dll was not found.");
		return false;
	}

	hostfxrLibrary_ = ::LoadLibraryW(hostfxrPath.c_str());
	if (!hostfxrLibrary_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to load hostfxr.dll.");
		return false;
	}

	auto loadFunction = [&](const char* name) -> void* {
		return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(hostfxrLibrary_), name));
		};

	hostfxrClose_ = reinterpret_cast<HostfxrCloseFn>(loadFunction("hostfxr_close"));
	auto initializeForRuntimeConfig =
		reinterpret_cast<HostfxrInitializeForRuntimeConfigFn>(loadFunction("hostfxr_initialize_for_runtime_config"));
	auto getRuntimeDelegate =
		reinterpret_cast<HostfxrGetRuntimeDelegateFn>(loadFunction("hostfxr_get_runtime_delegate"));

	if (!hostfxrClose_ || !initializeForRuntimeConfig || !getRuntimeDelegate) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr exports were not found.");
		return false;
	}

	HostfxrHandle context = nullptr;
	const std::filesystem::path runtimeConfigPath =
		scriptCoreAssemblyPath_.parent_path() / "NEM.ScriptCore.runtimeconfig.json";
	if (!std::filesystem::exists(runtimeConfigPath)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: runtimeconfig was not found. path={}", ToUtf8Path(runtimeConfigPath));
		return false;
	}

	int32_t result = initializeForRuntimeConfig(runtimeConfigPath.c_str(), nullptr, &context);
	if (result != 0 || !context) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr_initialize_for_runtime_config failed. code={}", result);
		return false;
	}

	result = getRuntimeDelegate(context, kLoadAssemblyAndGetFunctionPointer,
		reinterpret_cast<void**>(&loadAssemblyAndGetFunctionPointer_));
	hostfxrClose_(context);

	if (result != 0 || !loadAssemblyAndGetFunctionPointer_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to get load_assembly_and_get_function_pointer. code={}", result);
		return false;
	}
	return true;
}

bool Engine::ManagedScriptRuntime::InitRuntime() {
	return loadAssemblyAndGetFunctionPointer_ != nullptr;
}

bool Engine::ManagedScriptRuntime::LoadBridgeFunctions() {

	bool success = true;
	success &= LoadBridgeFunction(initializeNativeApi_, L"InitializeNativeApi");
	success &= LoadBridgeFunction(loadGameAssembly_, L"LoadGameAssembly");
	success &= LoadBridgeFunction(unloadGameAssembly_, L"UnloadGameAssembly");
	success &= LoadBridgeFunction(getScriptTypeCount_, L"GetScriptTypeCount");
	success &= LoadBridgeFunction(copyScriptTypeName_, L"CopyScriptTypeName");
	success &= LoadBridgeFunction(getSerializedFieldCount_, L"GetSerializedFieldCount");
	success &= LoadBridgeFunction(copySerializedFieldInfo_, L"CopySerializedFieldInfo");
	success &= LoadBridgeFunction(createInstance_, L"CreateInstance");
	success &= LoadBridgeFunction(setSerializedFields_, L"SetSerializedFields");
	success &= LoadBridgeFunction(destroyInstance_, L"DestroyInstance");
	success &= LoadBridgeFunction(invokeAwake_, L"InvokeAwake");
	success &= LoadBridgeFunction(invokeStart_, L"InvokeStart");
	success &= LoadBridgeFunction(invokeOnEnable_, L"InvokeOnEnable");
	success &= LoadBridgeFunction(invokeOnDisable_, L"InvokeOnDisable");
	success &= LoadBridgeFunction(invokeOnDestroy_, L"InvokeOnDestroy");
	success &= LoadBridgeFunction(invokeFixedUpdate_, L"InvokeFixedUpdate");
	success &= LoadBridgeFunction(invokeUpdate_, L"InvokeUpdate");
	success &= LoadBridgeFunction(invokeLateUpdate_, L"InvokeLateUpdate");
	success &= LoadBridgeFunction(invokeCollisionEnter_, L"InvokeCollisionEnter");
	success &= LoadBridgeFunction(invokeCollisionStay_, L"InvokeCollisionStay");
	success &= LoadBridgeFunction(invokeCollisionExit_, L"InvokeCollisionExit");
	return success;
}

bool Engine::ManagedScriptRuntime::LoadGameAssembly() {

	if (gameAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dll was not found.");
		return false;
	}
	if (!loadGameAssembly_) {
		return false;
	}

	const std::string path = ToUtf8Path(gameAssemblyPath_);
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: loading GameScripts.dll from {}", path);
	if (loadGameAssembly_(path.c_str()) == 0) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to load GameScripts.dll. path={}", path);
		return false;
	}
	return true;
}

void Engine::ManagedScriptRuntime::ReleaseHostfxr() {

	loadAssemblyAndGetFunctionPointer_ = nullptr;
	hostfxrClose_ = nullptr;
	if (hostfxrLibrary_) {
		::FreeLibrary(static_cast<HMODULE>(hostfxrLibrary_));
		hostfxrLibrary_ = nullptr;
	}
}

void Engine::ManagedScriptRuntime::Invoke(InvokeFn function, int32_t handle, const SystemContext& context) {

	if (!initialized_ || !function || handle == 0) {
		return;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	currentContext_ = &context;
	function(handle);
	currentContext_ = nullptr;
}

void Engine::ManagedScriptRuntime::InvokeCollision(InvokeCollisionFn function, int32_t handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {

	if (!initialized_ || !function || handle == 0) {
		return;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	currentContext_ = &context;
	function(handle, collision);
	currentContext_ = nullptr;
}
