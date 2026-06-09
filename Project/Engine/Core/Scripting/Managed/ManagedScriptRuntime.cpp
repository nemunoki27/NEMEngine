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
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <string_view>

//============================================================================
//	ManagedScriptRuntime classMethods
//============================================================================
namespace {

	// 現在のビルド設定名を返す
	std::string GetBuildProfile() {
		return _PROFILE;
	}

	// パスをUTF-8文字列へ変換する
	std::string ToUtf8Path(const std::filesystem::path& path) {
		return Engine::Algorithm::ConvertString(path.wstring());
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

			// _wdupenv_sが確保した領域は、wstringへコピーしたらコンストラクタ内で必ず解放する。
			// 解放はここだけで行い、デストラクタではwstring内部バッファに触れない
			wchar_t* previous = nullptr;
			size_t previousLength = 0;
			if (_wdupenv_s(&previous, &previousLength, name_) == 0 && previous) {

				// コピー中に例外が起きてもpreviousをリークしないようにする
				struct FreeGuard {
					wchar_t* pointer;
					~FreeGuard() { std::free(pointer); }
				} freeGuard{ previous };

				previousValue_ = previous;
				hadPreviousValue_ = true;
			}
			::SetEnvironmentVariableW(name_, value);
		}
		~ScopedEnvironmentVariableOverride() {

			// 復元はSetEnvironmentVariableWのみ。wstringが所有するバッファをfreeしてはいけない
			::SetEnvironmentVariableW(name_, hadPreviousValue_ ? previousValue_.c_str() : nullptr);
		}

		// コピー/ムーブ禁止。二重復元・二重解放を防ぐ
		ScopedEnvironmentVariableOverride(const ScopedEnvironmentVariableOverride&) = delete;
		ScopedEnvironmentVariableOverride& operator=(const ScopedEnvironmentVariableOverride&) = delete;
		ScopedEnvironmentVariableOverride(ScopedEnvironmentVariableOverride&&) = delete;
		ScopedEnvironmentVariableOverride& operator=(ScopedEnvironmentVariableOverride&&) = delete;
	private:
		const wchar_t* name_;
		bool hadPreviousValue_ = false;
		std::wstring previousValue_;
	};

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
		// LoadHostfxr内で確保したネイティブリソースは同関数内で解放済み
		return false;
	}

	if (!LoadBridgeFunctions()) {
		// 途中失敗でも半端なpointerやhostfxrハンドルを残さない
		Finalize();
		return false;
	}

	// ネイティブ側API（C++側の機能をC#から呼ぶための関数群）を初期化
	ManagedNativeApiTable callbacks{};
	// ABIヘッダを先頭に設定する。C#側はversion/size/capabilityを検証し、不一致なら初期化を拒否する
	callbacks.header.abiVersion = kManagedAbiVersion;
	callbacks.header.structSize = static_cast<uint32_t>(sizeof(ManagedNativeApiTable));
	callbacks.header.capabilities = kManagedCapabilitiesAll;
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

	if (!initializeNativeApi_ || initializeNativeApi_(&callbacks) != ManagedStatus::Ok) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to initialize native callbacks (ABI mismatch or managed exception).");
		Finalize();
		return false;
	}

	initialized_ = true;

	// 初期アセンブリをロード（現行ビルド出力）。Edit中の以降のreloadはManagedScriptBuildServiceが行う
	if (!ReloadGameAssembly()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dll was not loaded. Managed scripts will be unavailable.");
	}
	return true;
}

void Engine::ManagedScriptRuntime::Finalize() {

	UnloadGameAssembly();
	fieldCache_.clear();
	currentContext_ = nullptr;
	initialized_ = false;

	// 関数ポインタのリセット
	initializeNativeApi_ = nullptr;
	loadGameAssembly_ = nullptr;
	unloadGameAssembly_ = nullptr;
	getScriptTypeCount_ = nullptr;
	copyScriptTypeInfo_ = nullptr;
	generateScriptManifest_ = nullptr;
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
	lastManagedTypeCount_ = 0;

	if (!initialized_ || !getScriptTypeCount_ || !copyScriptTypeInfo_) {
		return;
	}

	int32_t typeCount = 0;
	if (getScriptTypeCount_(&typeCount) != ManagedStatus::Ok) {
		return;
	}
	lastManagedTypeCount_ = typeCount;
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: managed script type count={}", typeCount);
	for (int32_t i = 0; i < typeCount; ++i) {

		ManagedScriptTypeDescriptor descriptor{};
		if (copyScriptTypeInfo_(i, &descriptor) != ManagedStatus::Ok || descriptor.scriptTypeId[0] == '\0') {
			continue;
		}
		// Stable GUID を主キーに登録する。型名/sourcePath は表示・legacy照合・drag&drop用
		BehaviorTypeRegistry::GetInstance().RegisterManaged(
			descriptor.scriptTypeId, descriptor.fullTypeName, descriptor.displayName, descriptor.sourcePath);
		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptRuntime: registered managed script type={} id={}",
			descriptor.fullTypeName, descriptor.scriptTypeId);
	}
}

bool Engine::ManagedScriptRuntime::ReloadGameAssembly(bool waitForManagedDebugger) {

	// 現行ビルド出力(ResolveGameAssemblyPath)をロードする。初期ロード用。
	return LoadGameAssemblyFromPath(ResolveGameAssemblyPath(), waitForManagedDebugger);
}

bool Engine::ManagedScriptRuntime::LoadGameAssemblyFromPath(const std::filesystem::path& dllPath, bool waitForManagedDebugger) {

	if (!initialized_) {
		return false;
	}

	auto doReload = [this, &dllPath]() {
		UnloadGameAssembly();
		gameAssemblyPath_ = dllPath;
		if (!LoadGameAssembly()) {
			return false;
		}
		RefreshScriptTypes();
		return true;
	};

	if (waitForManagedDebugger) {
		// managed debuggerのattach待ちはユーザーの明示オプション。env経由でC#側へ伝える
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

std::filesystem::path Engine::ManagedScriptRuntime::GameScriptProjectPath() const {
	return ResolveGameScriptProjectPath();
}

Engine::ManagedScriptInstanceHandle Engine::ManagedScriptRuntime::CreateInstance(const std::string& scriptTypeId,
	ECSWorld& world, const Entity& entity, const nlohmann::json& serializedFields) {

	if (!initialized_ || !createInstance_) {
		return ManagedScriptInstanceHandle::Null();
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	ManagedScriptInstanceHandle createdHandle = ManagedScriptInstanceHandle::Null();
	const ManagedStatus status = createInstance_(scriptTypeId.c_str(), MakeNativeEntity(world, entity), json.c_str(), &createdHandle);
	// 生成失敗時は無効ハンドルを返す
	return status == ManagedStatus::Ok ? createdHandle : ManagedScriptInstanceHandle::Null();
}

void Engine::ManagedScriptRuntime::SetSerializedFields(ManagedScriptInstanceHandle handle, const nlohmann::json& serializedFields) {

	if (!initialized_ || !setSerializedFields_ || !handle.IsValid()) {
		return;
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	setSerializedFields_(handle, json.c_str());
}

void Engine::ManagedScriptRuntime::DestroyInstance(ManagedScriptInstanceHandle handle) {

	if (!initialized_ || !destroyInstance_ || !handle.IsValid()) {
		return;
	}
	destroyInstance_(handle);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeAwake(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	return Invoke(invokeAwake_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeStart(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	return Invoke(invokeStart_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnEnable(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	return Invoke(invokeOnEnable_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnDisable(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	return Invoke(invokeOnDisable_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnDestroy(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	return Invoke(invokeOnDestroy_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeFixedUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	return Invoke(invokeFixedUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	return Invoke(invokeUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeLateUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	return Invoke(invokeLateUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionEnter(ManagedScriptInstanceHandle handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	return InvokeCollision(invokeCollisionEnter_, handle, context, collision);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionStay(ManagedScriptInstanceHandle handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	return InvokeCollision(invokeCollisionStay_, handle, context, collision);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionExit(ManagedScriptInstanceHandle handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	return InvokeCollision(invokeCollisionExit_, handle, context, collision);
}

const std::vector<Engine::ManagedScriptField>& Engine::ManagedScriptRuntime::GetSerializedFields(const std::string& scriptTypeId) {

	static const std::vector<ManagedScriptField> kEmpty{};

	if (auto it = fieldCache_.find(scriptTypeId); it != fieldCache_.end()) {
		return it->second;
	}
	if (!initialized_ || !getSerializedFieldCount_ || !copySerializedFieldInfo_ || scriptTypeId.empty()) {
		return kEmpty;
	}

	int32_t fieldCount = 0;
	if (getSerializedFieldCount_(scriptTypeId.c_str(), &fieldCount) != ManagedStatus::Ok) {
		return kEmpty;
	}
	std::vector<ManagedScriptField> fields{};
	fields.reserve(std::max(0, fieldCount));

	for (int32_t i = 0; i < fieldCount; ++i) {

		ManagedNativeSerializedFieldInfo nativeInfo{};
		if (copySerializedFieldInfo_(scriptTypeId.c_str(), i, &nativeInfo) != ManagedStatus::Ok) {
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

	auto [it, inserted] = fieldCache_.emplace(scriptTypeId, std::move(fields));
	return it->second;
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::GenerateScriptManifest(
	const std::filesystem::path& assemblyPath, const std::filesystem::path& manifestOutputPath) {

	if (!initialized_ || !generateScriptManifest_) {
		return ManagedStatus::Unsupported;
	}
	// C#側が一時collectible ALCで対象DLLを反射し、検証してmanifest JSONを書き出す（現行DLLは触らない）
	const std::string dll = ToUtf8Path(assemblyPath);
	const std::string out = ToUtf8Path(manifestOutputPath);
	return generateScriptManifest_(dll.c_str(), out.c_str());
}

Engine::ManagedScriptRuntime& Engine::ManagedScriptRuntime::GetInstance() {
	static ManagedScriptRuntime runtime;
	return runtime;
}

bool Engine::ManagedScriptRuntime::LoadHostfxr() {

	// nethostのget_hostfxr_pathを使った公式フローでhostfxrを解決・初期化する。
	// 探索・ロード・デリゲート取得とRAIIによる失敗時cleanupはDotnetHostResolverに集約している。
	const std::filesystem::path runtimeConfigPath =
		scriptCoreAssemblyPath_.parent_path() / "NEM.ScriptCore.runtimeconfig.json";

	return dotnetHost_.Initialize(scriptCoreAssemblyPath_, runtimeConfigPath);
}

bool Engine::ManagedScriptRuntime::LoadBridgeFunctions() {

	bool success = true;
	success &= LoadBridgeFunction(initializeNativeApi_, L"InitializeNativeApi");
	success &= LoadBridgeFunction(loadGameAssembly_, L"LoadGameAssembly");
	success &= LoadBridgeFunction(unloadGameAssembly_, L"UnloadGameAssembly");
	success &= LoadBridgeFunction(getScriptTypeCount_, L"GetScriptTypeCount");
	success &= LoadBridgeFunction(copyScriptTypeInfo_, L"CopyScriptTypeInfo");
	success &= LoadBridgeFunction(generateScriptManifest_, L"GenerateScriptManifest");
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
	if (loadGameAssembly_(path.c_str()) != ManagedStatus::Ok) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to load GameScripts.dll. path={}", path);
		return false;
	}
	return true;
}

void Engine::ManagedScriptRuntime::ReleaseHostfxr() {

	// hostfxrライブラリの解放とデリゲート無効化はResolverのRAIIに委譲する。
	// Shutdownは複数回呼び出しても安全（Finalizeの多重呼び出しに対応）。
	dotnetHost_.Shutdown();
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::Invoke(InvokeFn function, ManagedScriptInstanceHandle handle, const SystemContext& context) {

	if (!initialized_ || !function || !handle.IsValid()) {
		return ManagedStatus::InvalidInstanceHandle;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	// contextはRAIIで設定し、C#側で例外が起きても確実に元へ戻す
	ScopedInvocationContext contextScope(context);
	return function(handle);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollision(InvokeCollisionFn function, ManagedScriptInstanceHandle handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {

	if (!initialized_ || !function || !handle.IsValid()) {
		return ManagedStatus::InvalidInstanceHandle;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	ScopedInvocationContext contextScope(context);
	return function(handle, collision);
}

//============================================================================
//	invocation contextのthread_local実体とRAIIガード
//============================================================================
thread_local const Engine::SystemContext* Engine::ManagedScriptRuntime::currentContext_ = nullptr;

const Engine::SystemContext* Engine::ManagedScriptRuntime::GetCurrentContext() {
	return currentContext_;
}

Engine::ManagedScriptRuntime::ScopedInvocationContext::ScopedInvocationContext(const SystemContext& context) :
	previous_(currentContext_) {
	// ネスト呼び出しに備えて以前のcontextを退避してから差し替える
	currentContext_ = &context;
}

Engine::ManagedScriptRuntime::ScopedInvocationContext::~ScopedInvocationContext() {
	currentContext_ = previous_;
}
