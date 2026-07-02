#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"
#include "Generated/ManagedComponentBindings.generated.h"
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>

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
#include <cmath>
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

	// 実行ファイルのあるディレクトリを返す、プレビルド配布ではここへ全ランタイムを配置する
	std::filesystem::path GetExecutableDirectory() {
		std::vector<wchar_t> buffer(1024);
		const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}
		return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
	}

	// ScriptCoreのアセンブリパスを解決
	std::filesystem::path ResolveScriptCoreAssemblyPath() {
		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		const std::filesystem::path exeDir = GetExecutableDirectory();
		const std::filesystem::path engineRoot = Engine::RuntimePaths::GetEngineProjectRoot().parent_path();
		return FindFirstExistingPath({
			exeDir / "Managed/NEM.ScriptCore.dll",
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
		const std::filesystem::path exeDir = GetExecutableDirectory();
		return FindFirstExistingPath({
			exeDir / "Managed/GameScripts.dll",
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

	// マネージドデバッグ環境の構成でJIT最適化抑制などを行う
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

			// _wdupenv_sが確保した領域はwstringへコピーしたらここで必ず解放し、デストラクタではwstring内部バッファに触れない
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

			// 復元はSetEnvironmentVariableWのみで、wstringが所有するバッファを解放してはいけない
			::SetEnvironmentVariableW(name_, hadPreviousValue_ ? previousValue_.c_str() : nullptr);
		}

		// コピーとムーブを禁止して二重復元と二重解放を防ぐ
		ScopedEnvironmentVariableOverride(const ScopedEnvironmentVariableOverride&) = delete;
		ScopedEnvironmentVariableOverride& operator=(const ScopedEnvironmentVariableOverride&) = delete;
		ScopedEnvironmentVariableOverride(ScopedEnvironmentVariableOverride&&) = delete;
		ScopedEnvironmentVariableOverride& operator=(ScopedEnvironmentVariableOverride&&) = delete;
	private:
		const wchar_t* name_;
		bool hadPreviousValue_ = false;
		std::wstring previousValue_;
	};

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

	// ネイティブ側APIつまりC++側の機能をC#から呼ぶための関数群を初期化する
	ManagedNativeApiTable callbacks{};
	// ABIヘッダを先頭に設定する、C#側はバージョンとサイズと機能を検証し不一致なら初期化を拒否する
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
	callbacks.getRotation = &ManagedScriptRuntime::GetRotationCallback;
	callbacks.setRotation = &ManagedScriptRuntime::SetRotationCallback;
	callbacks.getLossyScale = &ManagedScriptRuntime::GetLossyScaleCallback;
	callbacks.getComponentTypeId = &ManagedScriptRuntime::GetComponentTypeIdCallback;
	callbacks.hasComponent = &ManagedScriptRuntime::HasComponentCallback;
	callbacks.addComponent = &ManagedScriptRuntime::AddComponentCallback;
	callbacks.removeComponent = &ManagedScriptRuntime::RemoveComponentCallback;
	callbacks.destroyEntity = &ManagedScriptRuntime::DestroyEntityCallback;
	callbacks.getScriptEnabled = &ManagedScriptRuntime::GetScriptEnabledCallback;
	callbacks.setScriptEnabled = &ManagedScriptRuntime::SetScriptEnabledCallback;
	callbacks.getScriptInstance = &ManagedScriptRuntime::GetScriptInstanceCallback;
	// 自動生成コンポーネントバインディングの型付きプロパティ振り分け、ManagedComponentBindings.json由来
	callbacks.getComponentProperty = &GeneratedComponentBindings::GetComponentProperty;
	callbacks.setComponentProperty = &GeneratedComponentBindings::SetComponentProperty;
	callbacks.getComponentStringProperty = &GeneratedComponentBindings::GetComponentStringProperty;
	callbacks.setComponentStringProperty = &GeneratedComponentBindings::SetComponentStringProperty;
	// Gameplay v7のTime拡張とTimeScale
	callbacks.getUnscaledDeltaTime = &ManagedScriptRuntime::GetUnscaledDeltaTimeCallback;
	callbacks.getUnscaledFixedDeltaTime = &ManagedScriptRuntime::GetUnscaledFixedDeltaTimeCallback;
	callbacks.getTimeSinceStartup = &ManagedScriptRuntime::GetTimeSinceStartupCallback;
	callbacks.getUnscaledTime = &ManagedScriptRuntime::GetUnscaledTimeCallback;
	callbacks.getTimeScale = &ManagedScriptRuntime::GetTimeScaleCallback;
	callbacks.setTimeScale = &ManagedScriptRuntime::SetTimeScaleCallback;
	callbacks.getInputType = &ManagedScriptRuntime::GetInputTypeCallback;
	callbacks.setInputType = &ManagedScriptRuntime::SetInputTypeCallback;
	callbacks.getMouseRangeControl = &ManagedScriptRuntime::GetMouseRangeControlCallback;
	callbacks.setMouseRangeControl = &ManagedScriptRuntime::SetMouseRangeControlCallback;
	callbacks.setRendererMaterialColor = &ManagedScriptRuntime::SetRendererMaterialColorCallback;
	callbacks.getRendererMaterialColor = &ManagedScriptRuntime::GetRendererMaterialColorCallback;
	callbacks.fillMeshSetPositions = &ManagedScriptRuntime::FillMeshSetPositionsCallback;
	callbacks.getEntityReferenceIdentity = &ManagedScriptRuntime::GetEntityReferenceIdentityCallback;
	callbacks.getFrameCount = &ManagedScriptRuntime::GetFrameCountCallback;
	// Gameplay v7のAssetRef実行時解決
	callbacks.assetExists = &ManagedScriptRuntime::AssetExistsCallback;
	callbacks.copyAssetDisplayName = &ManagedScriptRuntime::CopyAssetDisplayNameCallback;
	// Gameplay v7のEntity生成/Prefab/Scene/SetParent
	callbacks.createEntity = &ManagedScriptRuntime::CreateEntityCallback;
	callbacks.instantiatePrefab = &ManagedScriptRuntime::InstantiatePrefabCallback;
	callbacks.loadSceneAdditive = &ManagedScriptRuntime::LoadSceneAdditiveCallback;
	callbacks.loadSceneSingle = &ManagedScriptRuntime::LoadSceneSingleCallback;
	callbacks.resolveEntityRef = &ManagedScriptRuntime::ResolveEntityRefCallback;
	// ライン描画v12のcomponent点列設定と即時描画
	callbacks.lineSetPoints = &ManagedScriptRuntime::LineSetPointsCallback;
	callbacks.lineDrawImmediate = &ManagedScriptRuntime::LineDrawImmediateCallback;
	callbacks.lineDrawSphereImmediate = &ManagedScriptRuntime::LineDrawSphereImmediateCallback;
	callbacks.lineAddPoint = &ManagedScriptRuntime::LineAddPointCallback;
	callbacks.lineUpdatePoint = &ManagedScriptRuntime::LineUpdatePointCallback;
	callbacks.unloadScene = &ManagedScriptRuntime::UnloadSceneCallback;
	callbacks.isSceneInstanceAlive = &ManagedScriptRuntime::IsSceneInstanceAliveCallback;
	callbacks.setParentKeepWorld = &ManagedScriptRuntime::SetParentKeepWorldCallback;
	// Gameplay v7の入力拡張で複数ゲームパッドや軸や文字や入力フォーカス
	callbacks.getGamepadButtonIndexed = &ManagedScriptRuntime::GetGamepadButtonIndexedCallback;
	callbacks.getGamepadButtonDownIndexed = &ManagedScriptRuntime::GetGamepadButtonDownIndexedCallback;
	callbacks.getGamepadButtonUpIndexed = &ManagedScriptRuntime::GetGamepadButtonUpIndexedCallback;
	callbacks.getGamepadAxis = &ManagedScriptRuntime::GetGamepadAxisCallback;
	callbacks.isGamepadConnectedIndexed = &ManagedScriptRuntime::IsGamepadConnectedIndexedCallback;
	callbacks.getConnectedGamepadCount = &ManagedScriptRuntime::GetConnectedGamepadCountCallback;
	callbacks.getHasFocus = &ManagedScriptRuntime::GetHasFocusCallback;
	callbacks.copyTextInput = &ManagedScriptRuntime::CopyTextInputCallback;
	callbacks.copyProjectRoot = &ManagedScriptRuntime::CopyProjectRootCallback;
	// Gameplay v7のAudioSourceメソッド
	callbacks.audioPlay = &ManagedScriptRuntime::AudioPlayCallback;
	callbacks.audioPause = &ManagedScriptRuntime::AudioPauseCallback;
	callbacks.audioStop = &ManagedScriptRuntime::AudioStopCallback;
	callbacks.audioIsPlaying = &ManagedScriptRuntime::AudioIsPlayingCallback;
	callbacks.reportScriptException = &ManagedScriptRuntime::ReportScriptExceptionCallback;
	// v14のTag公開とLayerマスク公開とEntity検索
	callbacks.copyTag = &ManagedScriptRuntime::CopyTagCallback;
	callbacks.setTag = &ManagedScriptRuntime::SetTagCallback;
	callbacks.getVisibilityLayerMask = &ManagedScriptRuntime::GetVisibilityLayerMaskCallback;
	callbacks.setVisibilityLayerMask = &ManagedScriptRuntime::SetVisibilityLayerMaskCallback;
	callbacks.getCollisionTypeMask = &ManagedScriptRuntime::GetCollisionTypeMaskCallback;
	callbacks.setCollisionTypeMask = &ManagedScriptRuntime::SetCollisionTypeMaskCallback;
	callbacks.findEntityByName = &ManagedScriptRuntime::FindEntityByNameCallback;
	callbacks.findEntityByTag = &ManagedScriptRuntime::FindEntityByTagCallback;
	callbacks.findEntitiesByTag = &ManagedScriptRuntime::FindEntitiesByTagCallback;
	callbacks.findEntityByComponent = &ManagedScriptRuntime::FindEntityByComponentCallback;
	callbacks.findEntitiesByComponent = &ManagedScriptRuntime::FindEntitiesByComponentCallback;
	callbacks.lineDrawShape = &ManagedScriptRuntime::LineDrawShapeCallback;
	// v16のTransform親追従の継承フラグ
	callbacks.getIgnoreParentRotation = &ManagedScriptRuntime::GetIgnoreParentRotationCallback;
	callbacks.setIgnoreParentRotation = &ManagedScriptRuntime::SetIgnoreParentRotationCallback;
	callbacks.getIgnoreParentScale = &ManagedScriptRuntime::GetIgnoreParentScaleCallback;
	callbacks.setIgnoreParentScale = &ManagedScriptRuntime::SetIgnoreParentScaleCallback;

	if (!initializeNativeApi_ || initializeNativeApi_(&callbacks) != ManagedStatus::Ok) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to initialize native callbacks (ABI mismatch or managed exception).");
		Finalize();
		return false;
	}

	initialized_ = true;

	// 初期アセンブリつまり現行ビルド出力をロードする、Edit中の以降のリロードはManagedScriptBuildServiceが行う
	if (!ReloadGameAssembly()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dll was not loaded. Managed scripts will be unavailable.");
	}
	return true;
}

void Engine::ManagedScriptRuntime::Finalize() {

	// アセンブリ解放より前にApplication.Quittingを発火する、解放で購読が解除されるため
	RaiseApplicationQuitting();

	UnloadGameAssembly();
	schemaCache_.clear();
	currentContext_ = nullptr;
	initialized_ = false;

	// 関数ポインタのリセット
	initializeNativeApi_ = nullptr;
	loadGameAssembly_ = nullptr;
	unloadGameAssembly_ = nullptr;
	pumpSceneEvents_ = nullptr;
	raiseApplicationQuitting_ = nullptr;
	tickFrame_ = nullptr;
	getLastAlcUnloadStatus_ = nullptr;
	getScriptTypeCount_ = nullptr;
	copyScriptTypeInfo_ = nullptr;
	generateScriptManifest_ = nullptr;
	getScriptSchemaJsonSize_ = nullptr;
	copyScriptSchemaJson_ = nullptr;
	getRuntimeStateSize_ = nullptr;
	copyRuntimeState_ = nullptr;
	setRuntimeField_ = nullptr;
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
	schemaCache_.clear();
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
		if (copyScriptTypeInfo_(i, &descriptor) != ManagedStatus::Ok || descriptor.scriptTypeID[0] == '\0') {
			continue;
		}
		// 安定GUIDを主キーに登録する、型名とソースパスは表示と旧照合とドラッグ用
		BehaviorTypeRegistry::GetInstance().RegisterManaged(
			descriptor.scriptTypeID, descriptor.fullTypeName, descriptor.displayName, descriptor.sourcePath,
			descriptor.defaultExecutionOrder);
		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptRuntime: registered managed script type={} id={}",
			descriptor.fullTypeName, descriptor.scriptTypeID);
	}
}

bool Engine::ManagedScriptRuntime::ReloadGameAssembly(bool waitForManagedDebugger) {

	// ResolveGameAssemblyPathの現行ビルド出力をロードする初期ロード用
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
		// マネージドデバッガのアタッチ待ちはユーザーの明示オプションで環境変数経由でC#側へ伝える
		ScopedEnvironmentVariableOverride waitOverride(L"NEM_MANAGED_WAIT_FOR_DEBUGGER", L"1");
		return doReload();
	}
	return doReload();
}

void Engine::ManagedScriptRuntime::UnloadGameAssembly() {

	schemaCache_.clear();
	BehaviorTypeRegistry::GetInstance().ClearManaged();

	if (unloadGameAssembly_) {
		unloadGameAssembly_();
	}
}

std::filesystem::path Engine::ManagedScriptRuntime::GameScriptProjectPath() const {
	return ResolveGameScriptProjectPath();
}

Engine::ManagedScriptInstanceHandle Engine::ManagedScriptRuntime::CreateInstance(const std::string& scriptTypeID,
	ECSWorld& world, const Entity& entity, const nlohmann::json& serializedFields, uint64_t scriptSlotID) {

	if (!initialized_ || !createInstance_) {
		return ManagedScriptInstanceHandle::Null();
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	ManagedScriptInstanceHandle createdHandle = ManagedScriptInstanceHandle::Null();
	const ManagedStatus status = createInstance_(scriptTypeID.c_str(), MakeNativeEntity(world, entity), json.c_str(),
		scriptSlotID, &createdHandle);
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

namespace {

	// スキーマJSONのkind文字列を列挙へ
	Engine::ManagedSerializedFieldKind ParseFieldKind(const std::string& kind) {

		using K = Engine::ManagedSerializedFieldKind;
		static const std::unordered_map<std::string, K> kMap = {
			{ "Bool", K::Bool }, { "Byte", K::Byte }, { "SByte", K::SByte }, { "Short", K::Short },
			{ "UShort", K::UShort }, { "Int", K::Int }, { "UInt", K::UInt }, { "Long", K::Long },
			{ "ULong", K::ULong }, { "Float", K::Float }, { "Double", K::Double }, { "String", K::String },
			{ "Enum", K::Enum }, { "Vector2", K::Vector2 }, { "Vector3", K::Vector3 }, { "Vector4", K::Vector4 },
			{ "Quaternion", K::Quaternion }, { "Color3", K::Color3 }, { "Color4", K::Color4 },
			{ "Nullable", K::Nullable }, { "Array", K::Array }, { "List", K::List },
			{ "AssetRef", K::AssetRef }, { "EntityRef", K::EntityRef }, { "ScriptRef", K::ScriptRef },
			{ "ComponentRef", K::ComponentRef },
		};
		auto it = kMap.find(kind);
		return it != kMap.end() ? it->second : K::Unsupported;
	}

	// 1フィールドのスキーマノードを解析する、配列やnullableは要素を再帰する
	Engine::ManagedFieldSchema ParseFieldSchema(const nlohmann::json& node) {

		Engine::ManagedFieldSchema field{};
		field.fieldID = node.value("fieldId", std::string{});
		field.name = node.value("name", std::string{});
		field.declaringType = node.value("declaringType", std::string{});
		field.kind = ParseFieldKind(node.value("kind", std::string("Unsupported")));
		field.isPublic = node.value("isPublic", false);
		field.isReadOnly = node.value("isReadOnly", false);
		field.isHidden = node.value("isHidden", false);
		field.multiline = node.value("multiline", false);
		field.tooltip = node.value("tooltip", std::string{});
		field.header = node.value("header", std::string{});
		field.label = node.value("label", std::string{});
		field.enumUnderlying = node.value("enumUnderlying", std::string{});
		field.assetType = node.value("assetType", std::string{});
		field.scriptType = node.value("scriptType", std::string{});
		field.componentType = node.value("componentType", std::string{});
		field.defaultValueJson = node.value("defaultValueJson", std::string("null"));

		if (node.contains("formerNames") && node["formerNames"].is_array()) {
			for (const auto& n : node["formerNames"]) {
				field.formerNames.push_back(n.get<std::string>());
			}
		}
		if (node.contains("range") && node["range"].is_object()) {
			field.hasRange = true;
			field.rangeMin = node["range"].value("min", 0.0f);
			field.rangeMax = node["range"].value("max", 0.0f);
		}
		if (node.contains("min") && node["min"].is_number()) {
			field.hasMin = true;
			field.minValue = node["min"].get<float>();
		}
		if (node.contains("dragSpeed") && node["dragSpeed"].is_number()) {
			field.hasDragSpeed = true;
			field.dragSpeed = node["dragSpeed"].get<float>();
		}
		if (node.contains("enumNames") && node["enumNames"].is_array()) {
			for (const auto& n : node["enumNames"]) {
				field.enumNames.push_back(n.get<std::string>());
			}
		}
		if (node.contains("enumValues") && node["enumValues"].is_array()) {
			for (const auto& v : node["enumValues"]) {
				field.enumValues.push_back(v.get<std::string>());
			}
		}
		if (node.contains("element") && node["element"].is_object()) {
			field.element = std::make_shared<Engine::ManagedFieldSchema>(ParseFieldSchema(node["element"]));
		}
		return field;
	}
}

const Engine::ManagedScriptSchema& Engine::ManagedScriptRuntime::GetScriptSchema(const std::string& scriptTypeID) {

	static const ManagedScriptSchema kEmpty{};

	if (scriptTypeID.empty()) {
		return kEmpty;
	}
	if (auto it = schemaCache_.find(scriptTypeID); it != schemaCache_.end()) {
		return it->second;
	}
	if (!initialized_ || !getScriptSchemaJsonSize_ || !copyScriptSchemaJson_) {
		return kEmpty;
	}

	// 二段階blobで必要サイズを取得してからvector確保してコピーする、固定長バッファを使わない
	int32_t size = 0;
	if (getScriptSchemaJsonSize_(scriptTypeID.c_str(), &size) != ManagedStatus::Ok || size <= 0) {
		return kEmpty;
	}
	std::string buffer(static_cast<size_t>(size), '\0');
	int32_t written = 0;
	if (copyScriptSchemaJson_(scriptTypeID.c_str(), buffer.data(), size, &written) != ManagedStatus::Ok) {
		return kEmpty;
	}
	buffer.resize(static_cast<size_t>(written));

	ManagedScriptSchema schema{};
	schema.scriptTypeID = scriptTypeID;
	try {
		nlohmann::json root = nlohmann::json::parse(buffer);
		schema.schemaVersion = root.value("schemaVersion", 0);
		schema.fullTypeName = root.value("fullTypeName", std::string{});
		if (root.contains("fields") && root["fields"].is_array()) {
			for (const auto& fieldNode : root["fields"]) {
				schema.fields.push_back(ParseFieldSchema(fieldNode));
			}
		}
	}
	catch (const nlohmann::json::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: failed to parse script schema for {}: {}", scriptTypeID, e.what());
	}

	auto [it, inserted] = schemaCache_.emplace(scriptTypeID, std::move(schema));
	return it->second;
}

nlohmann::json Engine::ManagedScriptRuntime::BuildSerializedValueMap(const std::string& scriptTypeID,
	const nlohmann::json& serializedFields) {

	// インスタンスへ適用するfieldGuidから値のマップを作る、新形式はそのまま旧形式は名前で移行する
	nlohmann::json result = nlohmann::json::object();
	if (!serializedFields.is_object()) {
		return result;
	}

	// 新形式{ fields: { guid: { name, type, value } } }
	if (serializedFields.contains("fields") && serializedFields["fields"].is_object()) {

		for (auto& [guid, entry] : serializedFields["fields"].items()) {
			if (entry.is_object() && entry.contains("value")) {
				result[guid] = entry["value"];
			} else {
				result[guid] = entry;
			}
		}
		return result;
	}

	// 旧形式の名前から値の形式で、スキーマの名前や旧名からguidを引いて移行する
	const ManagedScriptSchema& schema = GetScriptSchema(scriptTypeID);
	std::unordered_map<std::string, std::string> nameToGuid;
	for (const ManagedFieldSchema& field : schema.fields) {
		nameToGuid[field.name] = field.fieldID;
		for (const std::string& former : field.formerNames) {
			nameToGuid.emplace(former, field.fieldID);
		}
	}
	for (auto& [name, value] : serializedFields.items()) {
		auto it = nameToGuid.find(name);
		if (it != nameToGuid.end()) {
			result[it->second] = value;
		}
	}
	return result;
}

nlohmann::json Engine::ManagedScriptRuntime::GetRuntimeSerializedState(ManagedScriptInstanceHandle handle) {

	nlohmann::json empty = nlohmann::json::object();
	if (!initialized_ || !getRuntimeStateSize_ || !copyRuntimeState_ || !handle.IsValid()) {
		return empty;
	}

	int32_t size = 0;
	if (getRuntimeStateSize_(handle, &size) != ManagedStatus::Ok || size <= 0) {
		return empty;
	}
	std::string buffer(static_cast<size_t>(size), '\0');
	int32_t written = 0;
	if (copyRuntimeState_(handle, buffer.data(), size, &written) != ManagedStatus::Ok) {
		return empty;
	}
	buffer.resize(static_cast<size_t>(written));
	try {
		return nlohmann::json::parse(buffer);
	}
	catch (const nlohmann::json::exception&) {
		return empty;
	}
}

void Engine::ManagedScriptRuntime::SetRuntimeSerializedField(ManagedScriptInstanceHandle handle,
	const std::string& fieldID, const nlohmann::json& value) {

	if (!initialized_ || !setRuntimeField_ || !handle.IsValid() || fieldID.empty()) {
		return;
	}
	const std::string valueJson = value.dump();
	setRuntimeField_(handle, fieldID.c_str(), valueJson.c_str());
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::GenerateScriptManifest(
	const std::filesystem::path& assemblyPath, const std::filesystem::path& manifestOutputPath) {

	if (!initialized_ || !generateScriptManifest_) {
		return ManagedStatus::Unsupported;
	}
	// C#側が一時的な回収可能ALCで対象DLLを反射し検証してマニフェストJSONを書き出す、現行DLLは触らない
	const std::string dll = ToUtf8Path(assemblyPath);
	const std::string out = ToUtf8Path(manifestOutputPath);
	return generateScriptManifest_(dll.c_str(), out.c_str());
}

Engine::ManagedScriptRuntime& Engine::ManagedScriptRuntime::GetInstance() {
	static ManagedScriptRuntime runtime;
	return runtime;
}

bool Engine::ManagedScriptRuntime::LoadHostfxr() {

	// nethostのget_hostfxr_pathを使った公式フローでhostfxrを解決して初期化する
	const std::filesystem::path runtimeConfigPath =
		scriptCoreAssemblyPath_.parent_path() / "NEM.ScriptCore.runtimeconfig.json";

	return dotnetHost_.Initialize(scriptCoreAssemblyPath_, runtimeConfigPath);
}

bool Engine::ManagedScriptRuntime::LoadBridgeFunctions() {

	bool success = true;
	success &= LoadBridgeFunction(initializeNativeApi_, L"InitializeNativeApi");
	success &= LoadBridgeFunction(loadGameAssembly_, L"LoadGameAssembly");
	success &= LoadBridgeFunction(unloadGameAssembly_, L"UnloadGameAssembly");
	success &= LoadBridgeFunction(pumpSceneEvents_, L"PumpSceneEvents");
	success &= LoadBridgeFunction(raiseApplicationQuitting_, L"RaiseApplicationQuitting");
	success &= LoadBridgeFunction(tickFrame_, L"TickFrame");
	success &= LoadBridgeFunction(getLastAlcUnloadStatus_, L"GetLastAlcUnloadStatus");
	success &= LoadBridgeFunction(getScriptTypeCount_, L"GetScriptTypeCount");
	success &= LoadBridgeFunction(copyScriptTypeInfo_, L"CopyScriptTypeInfo");
	success &= LoadBridgeFunction(generateScriptManifest_, L"GenerateScriptManifest");
	success &= LoadBridgeFunction(getScriptSchemaJsonSize_, L"GetScriptSchemaJsonSize");
	success &= LoadBridgeFunction(copyScriptSchemaJson_, L"CopyScriptSchemaJson");
	success &= LoadBridgeFunction(getRuntimeStateSize_, L"GetRuntimeSerializedStateSize");
	success &= LoadBridgeFunction(copyRuntimeState_, L"CopyRuntimeSerializedState");
	success &= LoadBridgeFunction(setRuntimeField_, L"SetRuntimeSerializedField");
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

	// hostfxrライブラリの解放とデリゲート無効化はResolverのRAIIに委譲する、Shutdownは複数回呼び出しても安全でFinalizeの多重呼び出しに対応する
	dotnetHost_.Shutdown();
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::Invoke(InvokeFn function, ManagedScriptInstanceHandle handle, const SystemContext& context) {

	if (!initialized_ || !function || !handle.IsValid()) {
		return ManagedStatus::InvalidInstanceHandle;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	// コンテキストはRAIIで設定しC#側で例外が起きても確実に元へ戻す
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
//	呼び出しコンテキストのthread_local実体とRAIIガード
//============================================================================
thread_local const Engine::SystemContext* Engine::ManagedScriptRuntime::currentContext_ = nullptr;

// ゲーム時間サービスの状態でメインスレッドのみが更新する
float Engine::ManagedScriptRuntime::timeScale_ = 1.0f;
float Engine::ManagedScriptRuntime::scaledDeltaTime_ = 0.0f;
float Engine::ManagedScriptRuntime::unscaledDeltaTime_ = 0.0f;
float Engine::ManagedScriptRuntime::fixedDeltaTime_ = 1.0f / 60.0f;
double Engine::ManagedScriptRuntime::timeSinceStartup_ = 0.0;
double Engine::ManagedScriptRuntime::unscaledTime_ = 0.0;
uint64_t Engine::ManagedScriptRuntime::frameCount_ = 0;

const Engine::SystemContext* Engine::ManagedScriptRuntime::GetCurrentContext() {
	return currentContext_;
}

namespace {

	// NaNやinfは等速1.0へ、負値は0へ丸めて時間スケールを安全化する
	float SanitizeTimeScale(float value) {
		if (!std::isfinite(value)) {
			return 1.0f;
		}
		return value < 0.0f ? 0.0f : value;
	}
}

void Engine::ManagedScriptRuntime::BeginPlayTime(ECSWorld* playWorld) {

	// TimeScaleComponentがあれば初期スケールとして読み、最後に見つかった値を採用する
	timeScale_ = 1.0f;
	if (playWorld) {
		playWorld->ForEach<TimeScaleComponent>([&](Entity, TimeScaleComponent& component) {
			timeScale_ = SanitizeTimeScale(component.timeScale);
			});
	}
	scaledDeltaTime_ = 0.0f;
	unscaledDeltaTime_ = 0.0f;
	timeSinceStartup_ = 0.0;
	unscaledTime_ = 0.0;
	frameCount_ = 0;
}

float Engine::ManagedScriptRuntime::AdvanceTime(float rawDeltaTime, float fixedDeltaTime, bool advancing) {

	fixedDeltaTime_ = fixedDeltaTime;
	if (!advancing) {
		// Editや停止中は累積せずunscaledも進めない、Play側の時間のみを扱う
		scaledDeltaTime_ = 0.0f;
		unscaledDeltaTime_ = 0.0f;
		return 0.0f;
	}
	unscaledDeltaTime_ = rawDeltaTime;
	scaledDeltaTime_ = rawDeltaTime * timeScale_;
	unscaledTime_ += static_cast<double>(rawDeltaTime);
	timeSinceStartup_ += static_cast<double>(scaledDeltaTime_);
	++frameCount_;
	return scaledDeltaTime_;
}

void Engine::ManagedScriptRuntime::PumpSceneEvents() {

	// C#側でSceneのロード/アンロード完了を検出してSceneLoaded/SceneUnloadedを発火する
	if (pumpSceneEvents_) {
		pumpSceneEvents_();
	}
}

void Engine::ManagedScriptRuntime::RaiseApplicationQuitting() {

	// 終了処理前にC#のApplication.Quittingを一度だけ発火する
	if (raiseApplicationQuitting_) {
		raiseApplicationQuitting_();
	}
}

void Engine::ManagedScriptRuntime::TickFrame(int32_t phase, const SystemContext& context) {

	// TimerとCoroutineをメインスレッドで駆動する、phaseは0がUpdate 1がFixedUpdate 2がEndOfFrame
	if (tickFrame_) {
		// deltaTime参照のためcallback中だけコンテキストを設定する
		ScopedInvocationContext contextScope(context);
		tickFrame_(phase);
	}
}

Engine::AlcUnloadStatus Engine::ManagedScriptRuntime::GetLastAlcUnloadStatus() {

	if (!getLastAlcUnloadStatus_) {
		return AlcUnloadStatus::Unknown;
	}
	const int32_t status = getLastAlcUnloadStatus_();
	if (status == 1) {
		return AlcUnloadStatus::UnloadSucceeded;
	}
	if (status == 2) {
		return AlcUnloadStatus::LeakSuspected;
	}
	return AlcUnloadStatus::Unknown;
}

Engine::ManagedScriptRuntime::ScopedInvocationContext::ScopedInvocationContext(const SystemContext& context) :
	previous_(currentContext_) {
	// ネスト呼び出しに備えて以前のコンテキストを退避してから差し替える
	currentContext_ = &context;
}

Engine::ManagedScriptRuntime::ScopedInvocationContext::~ScopedInvocationContext() {
	currentContext_ = previous_;
}
