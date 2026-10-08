#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedRuntimePaths.h"
#include "ManagedBuildUtility.h"
#include "ManagedScriptUtility.h"
#include "Generated/ManagedComponentBindings.generated.h"
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Algorithm/EnvironmentUtility.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// c++
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <system_error>
#include <string_view>
// windows
#include <windows.h>

//============================================================================
//	ManagedScriptRuntime internal
//============================================================================
namespace {

	using Engine::ManagedBuildUtility::ToUtf8Path;

	// C#のデバッグ時に最適化を抑制
	void ConfigureManagedDebugEnvironment() {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		::SetEnvironmentVariableW(L"COMPlus_ReadyToRun", L"0");
		::SetEnvironmentVariableW(L"COMPlus_TieredCompilation", L"0");
		::SetEnvironmentVariableW(L"COMPlus_ZapDisable", L"1");
		::SetEnvironmentVariableW(L"DOTNET_EnableDiagnostics", L"1");
#endif
	}

	// スコープ終了時に環境変数を復元する
	class ScopedEnvironmentVariableOverride final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScopedEnvironmentVariableOverride(const wchar_t* name, const wchar_t* value) : name_(name) {

			// OS側の変更前の値を取得
			if (!Engine::Algorithm::TryReadProcessEnvironment(name_, previousValue_, hadPreviousValue_)) {
				Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
					"ManagedScriptRuntime: デバッグ環境の取得に失敗しました");
				return;
			}
			// 変更に成功した場合だけ復元対象にする
			changed_ = ::SetEnvironmentVariableW(name_, value) != FALSE;
			if (!changed_) {
				Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
					"ManagedScriptRuntime: デバッグ環境の変更に失敗しました error={}", ::GetLastError());
			}
		}
		~ScopedEnvironmentVariableOverride() {

			// 変更前の値へ戻す
			if (changed_ && !::SetEnvironmentVariableW(name_, hadPreviousValue_ ? previousValue_.c_str() : nullptr)) {
				Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
					"ManagedScriptRuntime: デバッグ環境の復元に失敗しました error={}", ::GetLastError());
			}
		}

		// コピーとムーブによる二重復元を禁止
		ScopedEnvironmentVariableOverride(const ScopedEnvironmentVariableOverride&) = delete;
		ScopedEnvironmentVariableOverride& operator=(const ScopedEnvironmentVariableOverride&) = delete;
		ScopedEnvironmentVariableOverride(ScopedEnvironmentVariableOverride&&) = delete;
		ScopedEnvironmentVariableOverride& operator=(ScopedEnvironmentVariableOverride&&) = delete;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 呼出中に借用する環境変数名
		const wchar_t* name_;
		// 変更前に値が存在したか
		bool hadPreviousValue_ = false;
		// このスコープで変更したか
		bool changed_ = false;
		// 変更前の値
		std::wstring previousValue_;
	};

} // namespace

//============================================================================
//	ManagedScriptRuntime classMethods
//============================================================================
bool Engine::ManagedScriptRuntime::Init() {

	if (initialized_) {
		return true;
	}

	ConfigureManagedDebugEnvironment();

	scriptCoreAssemblyPath_ = ManagedRuntimePaths::ResolveScriptCoreAssemblyPath();
	if (scriptCoreAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err, "ManagedScriptRuntime: NEM.ScriptCore.dllが見つかりません");
		return false;
	}
	Logger::Output(LogType::Engine, spdlog::level::info, "ManagedScriptRuntime: NEM.ScriptCore.dllを読み込みます path={}",
		ToUtf8Path(scriptCoreAssemblyPath_));

	if (!LoadHostfxr()) {
		// LoadHostfxr内で確保したネイティブリソースは同関数内で解放済み
		return false;
	}

	if (!LoadBridgeFunctions()) {
		// 途中失敗でも半端なpointerやhostfxrハンドルを残さない
		Finalize();
		return false;
	}

	// C#へ渡すNativeの呼出表を作成
	ManagedNativeAPITable callbacks = CreateNativeCallbacks();

	const ManagedStatus initializeStatus = bridge_.initializeNativeAPI_(&callbacks);
	if (initializeStatus != ManagedStatus::Ok) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: Native Callbackを初期化できません Status={} NativeABI={} APIサイズ={}",
			static_cast<int32_t>(initializeStatus), callbacks.header.abiVersion, callbacks.header.structSize);
		Finalize();
		return false;
	}

	initialized_ = true;

	// 起動時のゲームAssemblyを読み込む
	if (!ReloadGameAssembly()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dllを読み込めないためManaged Scriptを利用できません");
	}
	return true;
}

void Engine::ManagedScriptRuntime::Finalize() {

	// Assemblyの解放前に終了Eventを通知
	RaiseApplicationQuitting();
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();

	UnloadGameAssembly();
	schemaCache_.Clear();
	currentContext_ = nullptr;
	applicationQuitRequested_ = false;
	initialized_ = false;

	// 接続済みexportをまとめて解除する
	bridge_ = {};

	ReleaseHostfxr();
}

void Engine::ManagedScriptRuntime::RefreshScriptTypes() {

	BehaviorTypeRegistry::GetInstance().ClearManaged();
	schemaCache_.Clear();
	lastManagedTypeCount_ = 0;

	if (!initialized_ || !bridge_.getScriptTypeCount_ || !bridge_.copyScriptTypeInfo_) {
		return;
	}

	int32_t typeCount = 0;
	if (bridge_.getScriptTypeCount_(&typeCount) != ManagedStatus::Ok) {
		return;
	}
	lastManagedTypeCount_ = typeCount;
	Logger::Output(LogType::Engine, spdlog::level::info, "ManagedScriptRuntime: Managed Script型数={}", typeCount);
	for (int32_t i = 0; i < typeCount; ++i) {

		ManagedScriptTypeDescriptor descriptor{};
		if (bridge_.copyScriptTypeInfo_(i, &descriptor) != ManagedStatus::Ok || descriptor.scriptTypeID[0] == '\0') {
			continue;
		}
		// 型GUIDを主キーにScriptを登録
		BehaviorTypeRegistry::GetInstance().RegisterManaged(descriptor.scriptTypeID, descriptor.fullTypeName,
			descriptor.displayName, descriptor.sourcePath, descriptor.defaultExecutionOrder);
		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptRuntime: Managed Script型を登録しました type={} ID={}", descriptor.fullTypeName,
			descriptor.scriptTypeID);
	}
}

bool Engine::ManagedScriptRuntime::ReloadGameAssembly(bool waitForManagedDebugger) {

	// 現在の構築成果物からAssemblyを読み直す
	return LoadGameAssemblyFromPath(ManagedRuntimePaths::ResolveGameAssemblyPath(), waitForManagedDebugger);
}

bool Engine::ManagedScriptRuntime::LoadGameAssemblyFromPath(
	const std::filesystem::path& dllPath, bool waitForManagedDebugger) {

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
		// 選択時だけC#デバッガの接続を待つ
		ScopedEnvironmentVariableOverride waitOverride(L"NEM_MANAGED_WAIT_FOR_DEBUGGER", L"1");
		return doReload();
	}
	return doReload();
}

void Engine::ManagedScriptRuntime::UnloadGameAssembly() {

	ScriptProfiler::GetInstance().ResetOwners();
	gameAssemblyLoaded_ = false;
	schemaCache_.Clear();
	BehaviorTypeRegistry::GetInstance().ClearManaged();

	if (bridge_.unloadGameAssembly_) {
		bridge_.unloadGameAssembly_();
	}
}

std::filesystem::path Engine::ManagedScriptRuntime::GameScriptProjectPath() const {

	// ゲーム側の構築Projectを解決
	return ManagedRuntimePaths::ResolveGameScriptProjectPath();
}

const Engine::ManagedScriptSchema& Engine::ManagedScriptRuntime::GetScriptSchema(const std::string& scriptTypeID) {

	return schemaCache_.Get(scriptTypeID, initialized_, bridge_);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::GenerateScriptManifest(
	const std::filesystem::path& assemblyPath, const std::filesystem::path& manifestOutputPath) {

	if (!initialized_ || !bridge_.generateScriptManifest_) {
		return ManagedStatus::Unsupported;
	}
	// 専用ALCで型情報を収集してmanifestへ保存
	const std::string dll = ToUtf8Path(assemblyPath);
	const std::string out = ToUtf8Path(manifestOutputPath);
	return bridge_.generateScriptManifest_(dll.c_str(), out.c_str());
}

Engine::ManagedScriptRuntime& Engine::ManagedScriptRuntime::GetInstance() {

	// 共有Runtimeを返す
	static ManagedScriptRuntime runtime;
	return runtime;
}

bool Engine::ManagedScriptRuntime::LoadHostfxr() {

	// コアAssemblyの設定から.NETホストを初期化
	const std::filesystem::path runtimeConfigPath = scriptCoreAssemblyPath_.parent_path() / "NEM.ScriptCore.runtimeconfig.json";

	return dotnetHost_.Initialize(scriptCoreAssemblyPath_, runtimeConfigPath);
}

bool Engine::ManagedScriptRuntime::LoadBridgeFunctions() {

	return bridge_.Load(dotnetHost_, scriptCoreAssemblyPath_);
}

bool Engine::ManagedScriptRuntime::LoadGameAssembly() {

	if (gameAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "ManagedScriptRuntime: GameScripts.dllが見つかりません");
		return false;
	}
	if (!bridge_.loadGameAssembly_) {
		return false;
	}

	const std::string path = ToUtf8Path(gameAssemblyPath_);
	Logger::Output(LogType::Engine, spdlog::level::info, "ManagedScriptRuntime: GameScripts.dllを読み込みます path={}", path);
	if (bridge_.loadGameAssembly_(path.c_str()) != ManagedStatus::Ok) {
		Logger::Output(
			LogType::Engine, spdlog::level::err, "ManagedScriptRuntime: GameScripts.dllの読み込みに失敗しました path={}", path);
		return false;
	}
	gameAssemblyLoaded_ = true;
	return true;
}

void Engine::ManagedScriptRuntime::ReleaseHostfxr() {

	// .NETホストと接続済み関数を解放
	dotnetHost_.Shutdown();
}

//============================================================================
//	呼び出しコンテキストのthread_local実体とRAIIガード
//============================================================================
thread_local const Engine::SystemContext* Engine::ManagedScriptRuntime::currentContext_ = nullptr;
thread_local Engine::ECSWorld* Engine::ManagedScriptRuntime::currentReferenceWorld_ = nullptr;

// ゲーム時間サービスの状態でメインスレッドのみが更新する
float Engine::ManagedScriptRuntime::timeScale_ = 1.0f;
float Engine::ManagedScriptRuntime::scaledDeltaTime_ = 0.0f;
float Engine::ManagedScriptRuntime::unscaledDeltaTime_ = 0.0f;
float Engine::ManagedScriptRuntime::fixedDeltaTime_ = 1.0f / 60.0f;
double Engine::ManagedScriptRuntime::timeSinceStartup_ = 0.0;
double Engine::ManagedScriptRuntime::unscaledTime_ = 0.0;
uint64_t Engine::ManagedScriptRuntime::frameCount_ = 0;

const Engine::SystemContext* Engine::ManagedScriptRuntime::GetCurrentContext() {

	// 呼出中のContextを返す
	return currentContext_;
}

namespace {

	// 非有限の時間倍率を1へ、負値を0へ補正
	float SanitizeTimeScale(float value) {

		if (!std::isfinite(value)) {
			return 1.0f;
		}
		return value < 0.0f ? 0.0f : value;
	}
}

void Engine::ManagedScriptRuntime::BeginPlayTime(const ECSWorld* playWorld) {

	// SceneからPlay開始時の時間倍率を取得
	timeScale_ = 1.0f;
	if (playWorld) {
		playWorld->ForEach<TimeScaleComponent>(
			[&](Entity, const TimeScaleComponent& component) { timeScale_ = SanitizeTimeScale(component.timeScale); });
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
		// 停止中は差分時刻を0にする
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

void Engine::ManagedScriptRuntime::PumpSceneEvents(const SystemContext& context) {

	// Sceneのロードと解放をC#へ通知
	if (bridge_.pumpSceneEvents_) {
		ScopedInvocationContext contextScope(context);
		const uint64_t reportSequence = ManagedScriptExceptionStore::GetInstance().ReportSequence();
		CompleteManagedInvocation(bridge_.pumpSceneEvents_(), reportSequence);
	}
}

void Engine::ManagedScriptRuntime::RaiseApplicationQuitting() {

	// Applicationの終了EventをC#へ通知
	if (bridge_.raiseApplicationQuitting_) {
		bridge_.raiseApplicationQuitting_();
	}
}

bool Engine::ManagedScriptRuntime::ConsumeApplicationQuitRequest() {

	// 終了要求を取得して解除
	const bool requested = applicationQuitRequested_;
	applicationQuitRequested_ = false;
	return requested;
}

void Engine::ManagedScriptRuntime::RequestApplicationQuitCallback() {

	// フレーム終端で終了するように予約
	GetInstance().applicationQuitRequested_ = true;
}

void Engine::ManagedScriptRuntime::TickFrame(int32_t phase, const SystemContext& context) {

	// 指定phaseのTimerとCoroutineを更新
	if (bridge_.tickFrame_) {
		// callback中のContextを設定
		ScopedInvocationContext contextScope(context);
		const uint64_t reportSequence = ManagedScriptExceptionStore::GetInstance().ReportSequence();
		CompleteManagedInvocation(bridge_.tickFrame_(phase), reportSequence);
	}
}

Engine::ALCUnloadStatus Engine::ManagedScriptRuntime::GetLastALCUnloadStatus() {

	if (!bridge_.getLastALCUnloadStatus_) {
		return ALCUnloadStatus::Unknown;
	}
	const int32_t status = bridge_.getLastALCUnloadStatus_();
	if (status == 1) {
		return ALCUnloadStatus::UnloadSucceeded;
	}
	if (status == 2) {
		return ALCUnloadStatus::LeakSuspected;
	}
	return ALCUnloadStatus::Unknown;
}

Engine::ManagedScriptRuntime::ScopedInvocationContext::ScopedInvocationContext(const SystemContext& context)
	: previous_(currentContext_) {

	// 呼出中のContextへ切り替える
	currentContext_ = &context;
}

Engine::ManagedScriptRuntime::ScopedInvocationContext::~ScopedInvocationContext() {

	// 呼出前のContextへ戻す
	currentContext_ = previous_;
}

Engine::ManagedScriptRuntime::ScopedReferenceWorld::ScopedReferenceWorld(ECSWorld& world) : previous_(currentReferenceWorld_) {

	// 参照解決の対象Worldへ切り替える
	currentReferenceWorld_ = &world;
}

Engine::ManagedScriptRuntime::ScopedReferenceWorld::~ScopedReferenceWorld() {

	// 参照解決前のWorldへ戻す
	currentReferenceWorld_ = previous_;
}
