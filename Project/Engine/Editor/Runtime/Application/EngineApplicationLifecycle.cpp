#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
// c++
#include <stdexcept>

#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Runtime/Application/RuntimeSystemRegistration.h>
#include <Engine/Core/Runtime/Application/ApplicationSceneSettings.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

using namespace Engine;

namespace {

	constexpr const char* kFrameRateConfigPath = Engine::ConfigPaths::kFrameRate;
	// デフォルトマテリアル設定はチームで共有したいのでgit管理されるGameAssets配下へ置く
	constexpr const char* kDefaultMaterialConfigPath = "GameAssets/Materials/Config/defaultMaterials.materialSettings.json";

	Engine::EngineApplication* g_activeEngineApplication = nullptr;

	bool RequestEngineApplicationClose() {

		if (!g_activeEngineApplication) {
			return true;
		}
		return g_activeEngineApplication->RequestClose();
	}

	void NotifyEngineApplicationAssert() {

		if (!g_activeEngineApplication) {
			return;
		}
		g_activeEngineApplication->NotifyAssertBeforeAbort();
	}
}

void Engine::EngineApplication::InitSystems() {

	uiInputSystem_ = RegisterRuntimeSystems(scheduler_);
}

void Engine::EngineApplication::InitFirstScene() {

	if (!activeScene_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: アクティブシーンが設定されていません");
		return;
	}
	// アクティブなシーンの表示・保存用パスはGUIDから引き直す
	if (const AssetMeta* meta = assetDatabase_.Find(activeScene_)) {
		activeScenePath_ = meta->assetPath;
	}
	// シーンをロードしてエディタワールドにインスタンスを作成
	if (!editScenes_.LoadSceneTree(
		assetDatabase_, sceneSystem_, worldManager_.GetEditWorld(), activeScene_)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: アクティブシーンを読み込めません GUID={}",
			ToString(activeScene_));
		// 起動に失敗したWorldを更新せず終了処理へ戻す
		throw std::runtime_error("Startup scene loading failed");
	}
}

void Engine::EngineApplication::LoadActiveSceneConfig() {

	ApplicationSceneSettings::LoadEditor(assetDatabase_, activeScene_, activeScenePath_);
}

void Engine::EngineApplication::SaveActiveSceneConfig() const {

	ApplicationSceneSettings::Save(activeScene_, false);
}

void Engine::EngineApplication::Init(GraphicsCore& graphicsCore) {

	g_activeEngineApplication = this;
	WinApp::SetCloseRequestCallback(RequestEngineApplicationClose);
	Assert::SetPreAssertHandler(NotifyEngineApplicationAssert);

	// アセットデータベース初期化
	assetDatabase_.Init();
	assetDatabase_.RebuildMeta();
	LoadActiveSceneConfig();

	// フレームレート上限を設定ファイルから読み込む
	FrameRateSettings::GetInstance().Load(
		Algorithm::PathToUTF8(RuntimePaths::GetProjectSettingsPath(kFrameRateConfigPath)));
	FrameRateSettings::GetInstance().SetUseEditorTargetFps(true);
	// 描画タイプごとのデフォルトマテリアル設定をGameAssets配下から読み込む
	DefaultMaterialSettings::GetInstance().Load(
		Algorithm::PathToUTF8(RuntimePaths::GetGameRoot() / kDefaultMaterialConfigPath));
	// AnimationClipの評価に必要なPropertyをEditorの有無に関係なく登録する
	RegisterBuiltinAnimationProperties();

	// 骨アニメーション管理の初期化
	skinnedAnimationManager_.Init();
	// Audio管理の初期化
	Audio::GetInstance()->Init();

	// 最初のシーンを作成
	InitFirstScene();
	// C#スクリプトランタイム初期化
	managedStarted_ = true;
	ManagedScriptRuntime::GetInstance().Init();
	// EditWorldをスクリプトから参照可能にし生ポインタの代わりに世代付きハンドルを使う
	ManagedWorldRegistry::GetInstance().Register(worldManager_.GetEditWorld());
	if constexpr (BuildConfig::kEditorEnabled) {

		// Editモードの非同期build/reloadサービスを初期化しsource baselineとlast-known-goodを整える
		scriptBuildService_.Initialize(&ManagedScriptRuntime::GetInstance());
	}
	// システムの初期化
	InitSystems();

	// 描画パイプライン初期化
	renderPipeline_ = std::make_unique<RenderPipelineRunner>();
	renderPipeline_->Init();

	// ライン描画初期化
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	debugDrawingStarted_ = true;
	LineRenderer::GetInstance()->Init(graphicsCore);
#endif

	// エディタの初期化
	if constexpr (BuildConfig::kEditorEnabled) {

		editorManager_.Init(graphicsCore);

		// アセットの外部編集を非同期監視し、texture/modelを自動でホットリロードする
		assetWatchService_.Start(&assetDatabase_, &graphicsCore.GetTextureUploadService(),
			{ RuntimePaths::GetGameRoot() / "GameAssets", RuntimePaths::GetEngineAssetsRoot() });
		// モデル変更時のリロードは描画バックエンドのメッシュ管理へ委譲する
		assetWatchService_.SetMeshReloadCallback([this](AssetID meshAssetID) {
			MeshSubMeshAuthoring::InvalidateCachedLayout(meshAssetID);
			if (renderPipeline_) {
				renderPipeline_->ReloadMesh(meshAssetID);
			}
			});
		// 描画アセット変更時は種別に応じたランタイムキャッシュを再ロードする
		assetWatchService_.SetRenderAssetReloadCallback([this](AssetID assetID) {
			if (renderPipeline_) {
				renderPipeline_->ReloadAsset(assetDatabase_, assetID);
			}
			});
	} else {

		// Releaseはエディタ操作を待たず、起動時のシーンからPlayWorldを開始する
		StartPlayWorld();
		PreloadReleaseResources(graphicsCore);
	}
	initializationComplete_ = true;
}

void Engine::EngineApplication::Finalize() {

	if (initializationComplete_ && !shutdownAccepted_) {

		// WM_CLOSE以外の終了経路でも、最後に開いていたシーンだけは残す
		SaveActiveSceneConfig();
		shutdownAccepted_ = true;
	}
	WinApp::SetCloseRequestCallback(nullptr);
	Assert::SetPreAssertHandler(nullptr);

	if constexpr (BuildConfig::kEditorEnabled) {

		// WorldとAssetDatabaseを破棄する前に書き込み中のシーン保存を回収する
		WaitForSceneSave();
	}

	// アセット監視スレッドを止めてから他のリソースを解放する
	assetWatchService_.Stop();

	// 終了時点のWorldに合わせてSystemContextを更新してから切り離す
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;
	if (worldManager_.IsPlaying()) {
		StopPlayWorld();
	} else {
		scheduler_.DetachCurrentWorld(systemContext_);
	}

	// ランタイム管理クラスを描画パイプラインより先に終了する
	skinnedAnimationManager_.Finalize();

	// GPUリソースを持つ描画パイプラインを解放する
	if (renderPipeline_) {
		renderPipeline_->Finalize();
		renderPipeline_.reset();
	}

	if constexpr (BuildConfig::kEditorEnabled) {

		editorManager_.Finalize();
	}

	// ツールが持つGPUリソースをGraphicsCore終了前に確実に解放する
	ToolRegistry::GetInstance().Clear();

	if constexpr (BuildConfig::kEditorEnabled) {

		// Editモードのbuild/reloadサービスを停止し、実行中の子プロセスを安全に回収する
		scriptBuildService_.Shutdown();
	}
	// EditWorldの登録を解除してからC#ホストを解放する
	if (managedStarted_) {
		ManagedWorldRegistry::GetInstance().Unregister(
			ManagedWorldRegistry::GetInstance().TryGetHandle(worldManager_.GetEditWorld()));
	}
	// C#ホストと読み込んだアセンブリを解放する
	if (managedStarted_) {
		ManagedScriptRuntime::GetInstance().Finalize();
		managedStarted_ = false;
	}

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// デバッグライン描画リソースを解放する
	if (debugDrawingStarted_) {
		LineRenderer::GetInstance()->Finalize();
		debugDrawingStarted_ = false;
	}
#endif
}
