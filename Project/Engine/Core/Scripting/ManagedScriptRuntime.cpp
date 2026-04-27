#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/System/Context/SystemContext.h>
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Logger/Logger.h>
#include <Engine/Input/Input.h>
#include <Engine/Utility/Algorithm/Algorithm.h>

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

	// バージョンが新しいか
	bool IsNewerVersion(const std::wstring& candidate, const std::wstring& current) {

		return ParseVersion(current) < ParseVersion(candidate);
	}

	// 環境変数からパスを取得する
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

	// dotnetルートを取得する
	std::filesystem::path FindDotnetRoot() {

		if (auto path = GetEnvironmentPath(L"DOTNET_ROOT_X64"); !path.empty()) {
			return path;
		}
		if (auto path = GetEnvironmentPath(L"DOTNET_ROOT"); !path.empty()) {
			return path;
		}
		return L"C:/Program Files/dotnet";
	}

	// hostfxr.dllを探す
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

	// 存在する最初のパスを返す
	std::filesystem::path FindFirstExistingPath(const std::vector<std::filesystem::path>& paths) {

		for (const auto& path : paths) {
			if (std::filesystem::exists(path)) {
				return path;
			}
		}
		return {};
	}

	// Engine側のScriptCoreを探す
	std::filesystem::path ResolveScriptCoreAssemblyPath() {

		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		return FindFirstExistingPath({
			Engine::RuntimePaths::GetEngineLibraryRoot() / "Managed" / profile / "NEM.ScriptCore.dll",
			current / "Managed/NEM.ScriptCore.dll"
			});
	}

	// ゲーム側スクリプトアセンブリを探す
	std::filesystem::path ResolveGameAssemblyPath() {

		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		return FindFirstExistingPath({
			Engine::RuntimePaths::GetGameRoot() / "Managed" / profile / "GameScripts.dll",
			current / "Managed/GameScripts.dll"
			});
	}

	std::filesystem::path ResolveGameScriptProjectPath() {

		const std::filesystem::path current = std::filesystem::current_path();
		return FindFirstExistingPath({
			current / "Scripts/GameScripts.csproj",
			Engine::RuntimePaths::GetGameRoot() / "Scripts/GameScripts.csproj"
			});
	}

	void ConfigureManagedDebugEnvironment() {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		// JIT最適化関連を抑えて、C#ブレークポイントが止まりやすい状態にする
		::SetEnvironmentVariableW(L"COMPlus_ReadyToRun", L"0");
		::SetEnvironmentVariableW(L"COMPlus_TieredCompilation", L"0");
		::SetEnvironmentVariableW(L"COMPlus_ZapDisable", L"1");
		::SetEnvironmentVariableW(L"DOTNET_EnableDiagnostics", L"1");
#endif
	}

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
			std::free(previous);

			::SetEnvironmentVariableW(name_, value);
		}

		~ScopedEnvironmentVariableOverride() {

			if (hadPreviousValue_) {
				::SetEnvironmentVariableW(name_, previousValue_.c_str());
			} else {
				::SetEnvironmentVariableW(name_, nullptr);
			}
		}
	private:
		const wchar_t* name_ = nullptr;
		bool hadPreviousValue_ = false;
		std::wstring previousValue_{};
	};

	std::string MakeSnapshotKey(const std::filesystem::path& path) {

		return path.lexically_normal().generic_string();
	}

	void TryAddSnapshotFile(std::unordered_map<std::string, std::filesystem::file_time_type>& snapshot,
		const std::filesystem::path& path) {

		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec || !std::filesystem::is_regular_file(path, ec)) {
			return;
		}

		const auto writeTime = std::filesystem::last_write_time(path, ec);
		if (ec) {
			return;
		}
		snapshot[MakeSnapshotKey(path)] = writeTime;
	}

	void CollectScriptSnapshotFiles(std::unordered_map<std::string, std::filesystem::file_time_type>& snapshot,
		const std::filesystem::path& root) {

		std::error_code ec;
		if (!std::filesystem::exists(root, ec) || ec || !std::filesystem::is_directory(root, ec)) {
			return;
		}

		for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
			const std::filesystem::path filePath = it->path();
			if (it->is_directory(ec)) {
				const std::string name = filePath.filename().string();
				if (name == "obj" || name == "bin" || name == ".git") {
					it.disable_recursion_pending();
				}
				continue;
			}
			if (!it->is_regular_file(ec)) {
				continue;
			}
			if (filePath.extension() != ".cs") {
				continue;
			}
			TryAddSnapshotFile(snapshot, filePath);
		}
	}

	bool HasSnapshotChanged(
		const std::unordered_map<std::string, std::filesystem::file_time_type>& current,
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

	std::wstring QuoteCommandPath(const std::filesystem::path& path) {

		return L"\"" + path.wstring() + L"\"";
	}

	std::wstring ToWideAscii(const std::string& text) {

		return std::wstring(text.begin(), text.end());
	}

	// char配列からstd::stringへ変換する
	std::string MakeString(const char* text) {

		return text ? std::string(text) : std::string{};
	}

	// 名前空間付き型名からクラス名だけを取り出す
	std::string MakeSimpleTypeName(std::string_view typeName) {

		const size_t dot = typeName.find_last_of('.');
		if (dot == std::string_view::npos) {
			return std::string(typeName);
		}
		return std::string(typeName.substr(dot + 1));
	}

	// C#へ渡すEntityを作る
	Engine::ManagedNativeEntity MakeNativeEntity(Engine::ECSWorld& world, const Engine::Entity& entity) {

		Engine::ManagedNativeEntity native{};
		native.world = reinterpret_cast<std::uintptr_t>(&world);
		native.index = entity.index;
		native.generation = entity.generation;
		return native;
	}

	// C#から渡されたEntityを解決する
	Engine::ECSWorld* ResolveWorld(const Engine::ManagedNativeEntity& entity) {

		return reinterpret_cast<Engine::ECSWorld*>(entity.world);
	}

	Engine::Entity ResolveEntity(const Engine::ManagedNativeEntity& entity) {

		return Engine::Entity{ entity.index, entity.generation };
	}

	Engine::ManagedNativeEntity MakeNullNativeEntity() {

		return Engine::ManagedNativeEntity{};
	}

	Engine::ManagedVector2 ToManagedVector2(const Engine::Vector2& value) {

		return Engine::ManagedVector2{ value.x, value.y };
	}

	// Vector変換
	Engine::ManagedVector3 ToManagedVector3(const Engine::Vector3& value) {

		return Engine::ManagedVector3{ value.x, value.y, value.z };
	}

	Engine::Vector3 ToVector3(const Engine::ManagedVector3& value) {

		return Engine::Vector3{ value.x, value.y, value.z };
	}

	// Quaternion変換
	Engine::ManagedQuaternion ToManagedQuaternion(const Engine::Quaternion& value) {

		return Engine::ManagedQuaternion{ value.x, value.y, value.z, value.w };
	}

	Engine::Quaternion ToQuaternion(const Engine::ManagedQuaternion& value) {

		return Engine::Quaternion{ value.x, value.y, value.z, value.w };
	}

	// 親を考慮してワールド座標をローカル座標へ変換する
	Engine::Vector3 MakeLocalPositionFromWorld(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::Vector3& position) {

		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return position;
		}

		const Engine::Entity parent = world.GetComponent<Engine::HierarchyComponent>(entity).parent;
		if (!parent.IsValid() || !world.IsAlive(parent) || !world.HasComponent<Engine::TransformComponent>(parent)) {
			return position;
		}

		const Engine::Matrix4x4 inverseParent =
			Engine::Matrix4x4::Inverse(world.GetComponent<Engine::TransformComponent>(parent).worldMatrix);
		return Engine::Vector3::TransformPoint(position, inverseParent);
	}

	// トランスフォームを変更済みにする
	void MarkDirty(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::TransformComponent>(entity)) {
			return;
		}
		Engine::MarkTransformSubtreeDirty(world, entity);
	}

	Engine::SceneObjectComponent& EnsureScriptSceneObject(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.HasComponent<Engine::SceneObjectComponent>(entity)) {

			auto& sceneObject = world.AddComponent<Engine::SceneObjectComponent>(entity);
			sceneObject.localFileID = Engine::UUID::New();
			sceneObject.activeSelf = true;
			sceneObject.activeInHierarchy = true;
		}

		auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
		if (!sceneObject.localFileID) {
			sceneObject.localFileID = Engine::UUID::New();
		}
		return sceneObject;
	}

	void RefreshScriptActiveRecursive(Engine::ECSWorld& world, const Engine::Entity& entity, bool parentActive) {

		if (!world.IsAlive(entity)) {
			return;
		}

		auto& sceneObject = EnsureScriptSceneObject(world, entity);
		sceneObject.activeInHierarchy = parentActive && sceneObject.activeSelf;

		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return;
		}

		Engine::Entity child = world.GetComponent<Engine::HierarchyComponent>(entity).firstChild;
		while (child.IsValid() && world.IsAlive(child)) {

			RefreshScriptActiveRecursive(world, child, sceneObject.activeInHierarchy);
			if (!world.HasComponent<Engine::HierarchyComponent>(child)) {
				break;
			}
			child = world.GetComponent<Engine::HierarchyComponent>(child).nextSibling;
		}
	}

	void RefreshScriptActiveTree(Engine::ECSWorld& world, const Engine::Entity& entity) {

		bool parentActive = true;
		if (world.HasComponent<Engine::HierarchyComponent>(entity)) {

			const Engine::Entity parent = world.GetComponent<Engine::HierarchyComponent>(entity).parent;
			if (parent.IsValid() && world.IsAlive(parent)) {
				parentActive = Engine::IsEntityActiveInHierarchy(world, parent);
			}
		}
		RefreshScriptActiveRecursive(world, entity, parentActive);
	}

	int32_t CopyStringToBuffer(const std::string& text, char* buffer, int32_t capacity) {

		if (!buffer || capacity <= 0) {
			return 0;
		}

		const int32_t length = std::min(static_cast<int32_t>(text.size()), capacity - 1);
		if (0 < length) {
			std::memcpy(buffer, text.data(), static_cast<size_t>(length));
		}
		buffer[length] = '\0';
		return length;
	}
}

bool Engine::ManagedScriptRuntime::Init() {

	if (initialized_) {
		return true;
	}

	scriptCoreAssemblyPath_ = ResolveScriptCoreAssemblyPath();
	gameAssemblyPath_ = ResolveGameAssemblyPath();

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: ScriptCore path={}", ToUtf8Path(scriptCoreAssemblyPath_));
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: GameScripts path={}", ToUtf8Path(gameAssemblyPath_));

	if (scriptCoreAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: NEM.ScriptCore.dll was not found.");
		return false;
	}

	ConfigureManagedDebugEnvironment();

	if (!LoadHostfxr() || !InitRuntime() || !LoadBridgeFunctions()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to initialize .NET host bridge.");
		Finalize();
		return false;
	}

	ManagedNativeApiTable callbacks{};
	callbacks.getDeltaTime = &ManagedScriptRuntime::GetDeltaTimeCallback;
	callbacks.getFixedDeltaTime = &ManagedScriptRuntime::GetFixedDeltaTimeCallback;
	callbacks.log = &ManagedScriptRuntime::LogCallback;
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

	if (waitForManagedDebugger) {
		ScopedEnvironmentVariableOverride waitOverride(L"NEM_MANAGED_WAIT_FOR_DEBUGGER", L"1");
		UnloadGameAssembly();
		gameAssemblyPath_ = ResolveGameAssemblyPath();
		if (!LoadGameAssembly()) {
			return false;
		}

		RefreshScriptTypes();
		return true;
	}

	UnloadGameAssembly();
	gameAssemblyPath_ = ResolveGameAssemblyPath();
	if (!LoadGameAssembly()) {
		return false;
	}

	RefreshScriptTypes();
	return true;
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
	return inserted ? it->second : kEmpty;
}

bool Engine::ManagedScriptRuntime::TryResolveScriptTypeName(const std::string_view& scriptName, std::string& outTypeName) const {

	const std::string simpleName = MakeSimpleTypeName(scriptName);
	const auto& registry = BehaviorTypeRegistry::GetInstance();

	// まず完全一致を優先して、名前空間を含む指定でも曖昧にならないようにする
	for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

		const auto& info = registry.GetInfo(i);
		if (!info.managed) {
			continue;
		}
		if (info.name == scriptName) {
			outTypeName = info.name;
			return true;
		}
	}

	// アセット名は通常ファイル名なので、C#側のクラス名と照合する
	for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

		const auto& info = registry.GetInfo(i);
		if (!info.managed) {
			continue;
		}
		if (MakeSimpleTypeName(info.name) == simpleName) {
			outTypeName = info.name;
			return true;
		}
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

	currentContext_ = &context;
	function(handle);
	currentContext_ = nullptr;
}

float Engine::ManagedScriptRuntime::GetDeltaTimeCallback() {

	const SystemContext* context = GetInstance().currentContext_;
	return context ? context->deltaTime : 0.0f;
}

float Engine::ManagedScriptRuntime::GetFixedDeltaTimeCallback() {

	const SystemContext* context = GetInstance().currentContext_;
	return context ? context->fixedDeltaTime : 0.0f;
}

void Engine::ManagedScriptRuntime::LogCallback(int32_t level, const char* message) {

	spdlog::level::level_enum logLevel = spdlog::level::info;
	if (level == 1) {
		logLevel = spdlog::level::warn;
	} else if (level == 2) {
		logLevel = spdlog::level::err;
	}
	Logger::Output(LogType::GameLogic, logLevel, "{}", message ? message : "");
}

int32_t Engine::ManagedScriptRuntime::GetKeyCallback(int32_t key) {

	if (key < 0 || 255 < key) {
		return 0;
	}
	Input* input = Input::GetInstance();
	return input && input->PushKey(static_cast<BYTE>(key)) ? 1 : 0;
}

int32_t Engine::ManagedScriptRuntime::GetKeyDownCallback(int32_t key) {

	if (key < 0 || 255 < key) {
		return 0;
	}
	Input* input = Input::GetInstance();
	return input && input->TriggerKey(static_cast<BYTE>(key)) ? 1 : 0;
}

int32_t Engine::ManagedScriptRuntime::GetKeyUpCallback(int32_t key) {

	if (key < 0 || 255 < key) {
		return 0;
	}
	Input* input = Input::GetInstance();
	return input && input->ReleaseKey(static_cast<BYTE>(key)) ? 1 : 0;
}

int32_t Engine::ManagedScriptRuntime::GetMouseButtonCallback(int32_t button) {

	Input* input = Input::GetInstance();
	if (!input) {
		return 0;
	}

	switch (button) {
	case 0: return input->PushMouseLeft() ? 1 : 0;
	case 1: return input->PushMouseRight() ? 1 : 0;
	case 2: return input->PushMouseCenter() ? 1 : 0;
	default:
		return 0;
	}
}

int32_t Engine::ManagedScriptRuntime::GetMouseButtonDownCallback(int32_t button) {

	Input* input = Input::GetInstance();
	if (!input) {
		return 0;
	}

	switch (button) {
	case 0: return input->TriggerMouseLeft() ? 1 : 0;
	case 1: return input->TriggerMouseRight() ? 1 : 0;
	case 2: return input->TriggerMouseCenter() ? 1 : 0;
	default:
		return 0;
	}
}

int32_t Engine::ManagedScriptRuntime::GetMouseButtonUpCallback(int32_t button) {

	Input* input = Input::GetInstance();
	if (!input) {
		return 0;
	}

	switch (button) {
	case 0: return input->ReleaseMouse(MouseButton::Left) ? 1 : 0;
	case 1: return input->ReleaseMouse(MouseButton::Right) ? 1 : 0;
	case 2: return input->ReleaseMouse(MouseButton::Center) ? 1 : 0;
	default:
		return 0;
	}
}

Engine::ManagedVector2 Engine::ManagedScriptRuntime::GetMousePositionCallback() {

	Input* input = Input::GetInstance();
	return input ? ToManagedVector2(input->GetMousePos()) : ManagedVector2{};
}

Engine::ManagedVector2 Engine::ManagedScriptRuntime::GetMouseDeltaCallback() {

	Input* input = Input::GetInstance();
	return input ? ToManagedVector2(input->GetMouseMoveValue()) : ManagedVector2{};
}

float Engine::ManagedScriptRuntime::GetMouseWheelCallback() {

	Input* input = Input::GetInstance();
	return input ? input->GetMouseWheel() : 0.0f;
}

int32_t Engine::ManagedScriptRuntime::GetGamepadButtonCallback(int32_t button) {

	if (button < 0 || static_cast<int32_t>(GamePadButtons::Counts) <= button) {
		return 0;
	}
	Input* input = Input::GetInstance();
	return input && input->PushGamepadButton(static_cast<GamePadButtons>(button)) ? 1 : 0;
}

int32_t Engine::ManagedScriptRuntime::GetGamepadButtonDownCallback(int32_t button) {

	if (button < 0 || static_cast<int32_t>(GamePadButtons::Counts) <= button) {
		return 0;
	}
	Input* input = Input::GetInstance();
	return input && input->TriggerGamepadButton(static_cast<GamePadButtons>(button)) ? 1 : 0;
}

int32_t Engine::ManagedScriptRuntime::IsGamepadConnectedCallback() {

	Input* input = Input::GetInstance();
	return input && input->IsGamepadConnected() ? 1 : 0;
}

Engine::ManagedVector2 Engine::ManagedScriptRuntime::GetLeftStickCallback() {

	Input* input = Input::GetInstance();
	return input ? ToManagedVector2(input->GetLeftStickVal()) : ManagedVector2{};
}

Engine::ManagedVector2 Engine::ManagedScriptRuntime::GetRightStickCallback() {

	Input* input = Input::GetInstance();
	return input ? ToManagedVector2(input->GetRightStickVal()) : ManagedVector2{};
}

float Engine::ManagedScriptRuntime::GetLeftTriggerCallback() {

	Input* input = Input::GetInstance();
	return input ? input->GetLeftTriggerValue() : 0.0f;
}

float Engine::ManagedScriptRuntime::GetRightTriggerCallback() {

	Input* input = Input::GetInstance();
	return input ? input->GetRightTriggerValue() : 0.0f;
}

int32_t Engine::ManagedScriptRuntime::IsAliveCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	return (world && world->IsAlive(resolved)) ? 1 : 0;
}

int32_t Engine::ManagedScriptRuntime::CopyNameCallback(ManagedNativeEntity entity, char* buffer, int32_t capacity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<NameComponent>(resolved)) {
		return CopyStringToBuffer(std::string{}, buffer, capacity);
	}
	return CopyStringToBuffer(world->GetComponent<NameComponent>(resolved).name, buffer, capacity);
}

void Engine::ManagedScriptRuntime::SetNameCallback(ManagedNativeEntity entity, const char* name) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved)) {
		return;
	}

	if (!world->HasComponent<NameComponent>(resolved)) {
		world->AddComponent<NameComponent>(resolved);
	}
	world->GetComponent<NameComponent>(resolved).name = name ? std::string(name) : std::string{};
}

int32_t Engine::ManagedScriptRuntime::GetActiveSelfCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<SceneObjectComponent>(resolved)) {
		return 1;
	}
	return world->GetComponent<SceneObjectComponent>(resolved).activeSelf ? 1 : 0;
}

void Engine::ManagedScriptRuntime::SetActiveSelfCallback(ManagedNativeEntity entity, int32_t active) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved)) {
		return;
	}

	EnsureScriptSceneObject(*world, resolved).activeSelf = active != 0;
	RefreshScriptActiveTree(*world, resolved);
}

int32_t Engine::ManagedScriptRuntime::GetActiveInHierarchyCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	return (world && IsEntityActiveInHierarchy(*world, resolved)) ? 1 : 0;
}

Engine::ManagedNativeEntity Engine::ManagedScriptRuntime::GetParentCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<HierarchyComponent>(resolved)) {
		return MakeNullNativeEntity();
	}

	const Entity parent = world->GetComponent<HierarchyComponent>(resolved).parent;
	return world->IsAlive(parent) ? MakeNativeEntity(*world, parent) : MakeNullNativeEntity();
}

Engine::ManagedNativeEntity Engine::ManagedScriptRuntime::GetFirstChildCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<HierarchyComponent>(resolved)) {
		return MakeNullNativeEntity();
	}

	const Entity child = world->GetComponent<HierarchyComponent>(resolved).firstChild;
	return world->IsAlive(child) ? MakeNativeEntity(*world, child) : MakeNullNativeEntity();
}

Engine::ManagedNativeEntity Engine::ManagedScriptRuntime::GetNextSiblingCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<HierarchyComponent>(resolved)) {
		return MakeNullNativeEntity();
	}

	const Entity sibling = world->GetComponent<HierarchyComponent>(resolved).nextSibling;
	return world->IsAlive(sibling) ? MakeNativeEntity(*world, sibling) : MakeNullNativeEntity();
}

void Engine::ManagedScriptRuntime::SetParentCallback(ManagedNativeEntity entity, ManagedNativeEntity parent) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity child = ResolveEntity(entity);
	if (!world || !world->IsAlive(child)) {
		return;
	}

	const Entity newParent = ResolveWorld(parent) == world ? ResolveEntity(parent) : Entity::Null();
	HierarchySystem hierarchySystem{};
	hierarchySystem.SetParent(*world, child, world->IsAlive(newParent) ? newParent : Entity::Null());
}

Engine::ManagedVector3 Engine::ManagedScriptRuntime::GetPositionCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return {};
	}
	return ToManagedVector3(world->GetComponent<TransformComponent>(resolved).worldMatrix.GetTranslationValue());
}

void Engine::ManagedScriptRuntime::SetPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return;
	}

	auto& transform = world->GetComponent<TransformComponent>(resolved);
	transform.localPos = MakeLocalPositionFromWorld(*world, resolved, ToVector3(value));
	MarkDirty(*world, resolved);
}

Engine::ManagedVector3 Engine::ManagedScriptRuntime::GetLocalPositionCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return {};
	}
	return ToManagedVector3(world->GetComponent<TransformComponent>(resolved).localPos);
}

void Engine::ManagedScriptRuntime::SetLocalPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return;
	}

	world->GetComponent<TransformComponent>(resolved).localPos = ToVector3(value);
	MarkDirty(*world, resolved);
}

Engine::ManagedVector3 Engine::ManagedScriptRuntime::GetLocalScaleCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return ManagedVector3{ 1.0f, 1.0f, 1.0f };
	}
	return ToManagedVector3(world->GetComponent<TransformComponent>(resolved).localScale);
}

void Engine::ManagedScriptRuntime::SetLocalScaleCallback(ManagedNativeEntity entity, ManagedVector3 value) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return;
	}

	world->GetComponent<TransformComponent>(resolved).localScale = ToVector3(value);
	MarkDirty(*world, resolved);
}

Engine::ManagedQuaternion Engine::ManagedScriptRuntime::GetLocalRotationCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return {};
	}
	return ToManagedQuaternion(world->GetComponent<TransformComponent>(resolved).localRotation);
}

void Engine::ManagedScriptRuntime::SetLocalRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return;
	}

	world->GetComponent<TransformComponent>(resolved).localRotation = Quaternion::Normalize(ToQuaternion(value));
	MarkDirty(*world, resolved);
}
