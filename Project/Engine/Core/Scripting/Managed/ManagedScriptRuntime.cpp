#include "ManagedScriptRuntime.h"
#include "ManagedRuntimePaths.h"
#include "ManagedScriptUtility.h"
#include "Generated/ManagedComponentBindings.generated.h"
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
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

	// パスをUTF-8文字列へ変換する
	std::string ToUtf8Path(const std::filesystem::path& path) {
		return Engine::Algorithm::ConvertString(path.wstring());
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

	scriptCoreAssemblyPath_ = ManagedRuntimePaths::ResolveScriptCoreAssemblyPath();
	if (scriptCoreAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: NEM.ScriptCore.dllが見つかりません");
		return false;
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: NEM.ScriptCore.dllを読み込みます path={}",
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

	// ネイティブ側APIつまりC++側の機能をC#から呼ぶための関数群を初期化する
	ManagedNativeAPITable callbacks = CreateNativeCallbacks();

	const ManagedStatus initializeStatus = bridge_.initializeNativeAPI_ ?
		bridge_.initializeNativeAPI_(&callbacks) : ManagedStatus::Unsupported;
	if (initializeStatus != ManagedStatus::Ok) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: Native Callbackを初期化できません Status={} NativeABI={} APIサイズ={}",
			static_cast<int32_t>(initializeStatus), callbacks.header.abiVersion, callbacks.header.structSize);
		Finalize();
		return false;
	}

	initialized_ = true;

	// 初期アセンブリつまり現行ビルド出力をロードする、Edit中の以降のリロードはManagedScriptBuildServiceが行う
	if (!ReloadGameAssembly()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dllを読み込めないためManaged Scriptを利用できません");
	}
	return true;
}

void Engine::ManagedScriptRuntime::Finalize() {

	// アセンブリ解放より前にApplication.Quittingを発火する、解放で購読が解除されるため
	RaiseApplicationQuitting();
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();

	UnloadGameAssembly();
	schemaCache_.Clear();
	currentContext_ = nullptr;
	applicationQuitRequested_ = false;
	initialized_ = false;

	// 関数ポインタのリセット
	bridge_.initializeNativeAPI_ = nullptr;
	bridge_.loadGameAssembly_ = nullptr;
	bridge_.unloadGameAssembly_ = nullptr;
	bridge_.pumpSceneEvents_ = nullptr;
	bridge_.raiseApplicationQuitting_ = nullptr;
	bridge_.tickFrame_ = nullptr;
	bridge_.configureProfiler_ = nullptr;
	bridge_.getLastAlcUnloadStatus_ = nullptr;
	bridge_.getScriptTypeCount_ = nullptr;
	bridge_.copyScriptTypeInfo_ = nullptr;
	bridge_.generateScriptManifest_ = nullptr;
	bridge_.getScriptSchemaJsonSize_ = nullptr;
	bridge_.copyScriptSchemaJson_ = nullptr;
	bridge_.getRuntimeStateSize_ = nullptr;
	bridge_.copyRuntimeState_ = nullptr;
	bridge_.setRuntimeField_ = nullptr;
	bridge_.createInstance_ = nullptr;
	bridge_.setSerializedFields_ = nullptr;
	bridge_.flushPendingReferences_ = nullptr;
	bridge_.destroyInstance_ = nullptr;
	bridge_.invokeAwake_ = nullptr;
	bridge_.invokeStart_ = nullptr;
	bridge_.invokeOnEnable_ = nullptr;
	bridge_.invokeOnDisable_ = nullptr;
	bridge_.invokeOnDestroy_ = nullptr;
	bridge_.invokeFixedUpdate_ = nullptr;
	bridge_.invokeUpdate_ = nullptr;
	bridge_.invokeLateUpdate_ = nullptr;
	bridge_.invokeCollisionEnter_ = nullptr;
	bridge_.invokeCollisionStay_ = nullptr;
	bridge_.invokeCollisionExit_ = nullptr;
	bridge_.invokeAnimationEvent_ = nullptr;

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
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: Managed Script型数={}", typeCount);
	for (int32_t i = 0; i < typeCount; ++i) {

		ManagedScriptTypeDescriptor descriptor{};
		if (bridge_.copyScriptTypeInfo_(i, &descriptor) != ManagedStatus::Ok || descriptor.scriptTypeID[0] == '\0') {
			continue;
		}
		// 安定GUIDを主キーに登録する、型名とソースパスは表示と旧照合とドラッグ用
		BehaviorTypeRegistry::GetInstance().RegisterManaged(
			descriptor.scriptTypeID, descriptor.fullTypeName, descriptor.displayName, descriptor.sourcePath,
			descriptor.defaultExecutionOrder);
		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptRuntime: Managed Script型を登録しました type={} ID={}",
			descriptor.fullTypeName, descriptor.scriptTypeID);
	}
}

bool Engine::ManagedScriptRuntime::ReloadGameAssembly(bool waitForManagedDebugger) {

	// ResolveGameAssemblyPathの現行ビルド出力をロードする初期ロード用
	return LoadGameAssemblyFromPath(ManagedRuntimePaths::ResolveGameAssemblyPath(), waitForManagedDebugger);
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

	ScriptProfiler::GetInstance().ResetOwners();
	gameAssemblyLoaded_ = false;
	schemaCache_.Clear();
	BehaviorTypeRegistry::GetInstance().ClearManaged();

	if (bridge_.unloadGameAssembly_) {
		bridge_.unloadGameAssembly_();
	}
}

std::filesystem::path Engine::ManagedScriptRuntime::GameScriptProjectPath() const {
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
	// C#側が一時的な回収可能ALCで対象DLLを反射し検証してマニフェストJSONを書き出す、現行DLLは触らない
	const std::string dll = ToUtf8Path(assemblyPath);
	const std::string out = ToUtf8Path(manifestOutputPath);
	return bridge_.generateScriptManifest_(dll.c_str(), out.c_str());
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

	return bridge_.Load(dotnetHost_, scriptCoreAssemblyPath_);
}

bool Engine::ManagedScriptRuntime::LoadGameAssembly() {

	if (gameAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dllが見つかりません");
		return false;
	}
	if (!bridge_.loadGameAssembly_) {
		return false;
	}

	const std::string path = ToUtf8Path(gameAssemblyPath_);
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: GameScripts.dllを読み込みます path={}", path);
	if (bridge_.loadGameAssembly_(path.c_str()) != ManagedStatus::Ok) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: GameScripts.dllの読み込みに失敗しました path={}", path);
		return false;
	}
	gameAssemblyLoaded_ = true;
	return true;
}

void Engine::ManagedScriptRuntime::ReleaseHostfxr() {

	// hostfxrライブラリの解放とデリゲート無効化はResolverのRAIIに委譲する、Shutdownは複数回呼び出しても安全でFinalizeの多重呼び出しに対応する
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
	if (bridge_.pumpSceneEvents_) {
		bridge_.pumpSceneEvents_();
	}
}

void Engine::ManagedScriptRuntime::RaiseApplicationQuitting() {

	// 終了処理前にC#のApplication.Quittingを一度だけ発火する
	if (bridge_.raiseApplicationQuitting_) {
		bridge_.raiseApplicationQuitting_();
	}
}

bool Engine::ManagedScriptRuntime::ConsumeApplicationQuitRequest() {

	const bool requested = applicationQuitRequested_;
	applicationQuitRequested_ = false;
	return requested;
}

void Engine::ManagedScriptRuntime::RequestApplicationQuitCallback() {
	GetInstance().applicationQuitRequested_ = true;
}

void Engine::ManagedScriptRuntime::TickFrame(int32_t phase, const SystemContext& context) {

	// TimerとCoroutineをメインスレッドで駆動する、phaseは0がUpdate 1がFixedUpdate 2がEndOfFrame
	if (bridge_.tickFrame_) {
		// deltaTime参照のためcallback中だけコンテキストを設定する
		ScopedInvocationContext contextScope(context);
		bridge_.tickFrame_(phase);
	}
}

Engine::AlcUnloadStatus Engine::ManagedScriptRuntime::GetLastAlcUnloadStatus() {

	if (!bridge_.getLastAlcUnloadStatus_) {
		return AlcUnloadStatus::Unknown;
	}
	const int32_t status = bridge_.getLastAlcUnloadStatus_();
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

Engine::ManagedScriptRuntime::ScopedReferenceWorld::ScopedReferenceWorld(ECSWorld& world) :
	previous_(currentReferenceWorld_) {

	currentReferenceWorld_ = &world;
}

Engine::ManagedScriptRuntime::ScopedReferenceWorld::~ScopedReferenceWorld() {
	currentReferenceWorld_ = previous_;
}
