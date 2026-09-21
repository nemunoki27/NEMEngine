#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include "EditorRenderRequestBuilder.h"
#include "EditorPlaySession.h"
#include "SceneSaveController.h"
#include "PrefabEditSession.h"
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Outline/EditorSelectionOutlineRequestService.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Core/Runtime/Application/ApplicationPreloader.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>

using namespace Engine;

//============================================================================
//	EngineApplication classMethods
//============================================================================

void Engine::EngineApplication::PreloadReleaseResources(GraphicsCore& graphicsCore) {

	ApplicationPreloadContext context{ assetDatabase_, sceneSystem_, *renderPipeline_, skinnedAnimationManager_,
		animationClipManager_, systemContext_, worldManager_, playScenes_, runtimeWorldBaker_, activeScene_,
		[this]() { RefreshActiveWorldContext(); } };
	if (ApplicationPreloader::Run(graphicsCore, context, true)) {
		requestFrameDeltaReset_ = true;
		playSession_->SetJustStarted();
	}
}

void Engine::EngineApplication::Tick(GraphicsCore& graphicsCore, float deltaTime) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// フレームごとのデバッグラインをリセットする
	LineRenderer::GetInstance()->BeginFrame();
	// 選択アウトラインのtemporary requestもフレーム単位でリセットする
	EditorSelectionOutlineRequestService::GetInstance().BeginFrame();
#endif

	if constexpr (BuildConfig::kEditorEnabled) {

		// 完了した非同期保存をメインスレッドのAssetDatabaseと未保存状態へ反映する
		UpdateSceneSave();
	}

	// アセットの外部編集を非同期検知し、変更があればtexture/modelをホットリロードする
	assetWatchService_.Update();

	// システムコンテキストの更新
	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.deltaTime = deltaTime;
	systemContext_.assetDatabase = &assetDatabase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.animationClipManager = &animationClipManager_;
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;

	if constexpr (BuildConfig::kEditorEnabled) {

		// 非同期build/reload状態機械を進める、Play中はreloadを適用せず変更検知のdirtyのみ行う
		scriptBuildService_.Tick(worldManager_.IsPlaying());
	}

	// プレイモードの切り替え
	HandlePlayToggle();
	// Play中の一時停止、再開、1フレーム送りを処理する
	HandlePlayPauseRequests();
	// エディタから要求されたシーン操作
	HandleEditorSceneRequests();
	// Play/Stopやシーン操作後のActive Worldを、このフレームの各Contextへ反映する
	RefreshActiveWorldContext();
	{
		// Play開始直後の最初の1フレームは進めず、貫通の原因になる大きなdeltaを捨てる
		const bool skipFirstAdvance = playSession_->ConsumeJustStarted();

		bool advancePlayTime = !skipFirstAdvance && ShouldAdvanceActiveWorld() && systemContext_.mode == WorldMode::Play;
		float rawDelta = (!skipFirstAdvance && ShouldAdvanceActiveWorld()) ? deltaTime : 0.0f;
		systemContext_.deltaTime = ManagedScriptRuntime::AdvanceTime(rawDelta, systemContext_.fixedDeltaTime, advancePlayTime);
		systemContext_.unscaledDeltaTime = rawDelta;
		const float timeDelta = systemContext_.mode == WorldMode::Play ?
			systemContext_.deltaTime : rawDelta;
		systemContext_.time += timeDelta;
		systemContext_.unscaledTime += rawDelta;
		if (0.0f < rawDelta) {
			const float smoothWeight =
				(std::clamp)(rawDelta * 8.0f, 0.0f, 1.0f);
			systemContext_.smoothDeltaTime =
				systemContext_.smoothDeltaTime <= 0.0f ?
				timeDelta :
				systemContext_.smoothDeltaTime +
				(timeDelta - systemContext_.smoothDeltaTime) *
				smoothWeight;
		}
	}

	ECSWorld* world = systemContext_.world;
	const SceneHeader* header = systemContext_.activeSceneHeader;

	if constexpr (BuildConfig::kEditorEnabled) {

		// パネルをすべて非表示にする
		bool hidePanels = editorManager_.GetLayoutState().hidePanels;
		if (hidePanels) {

			const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
			const DXGI_SWAP_CHAIN_DESC1& swapChainDesc = graphicsCore.GetSwapChainDesc();
			Input::GetInstance()->SetViewRect(InputViewArea::Game, Vector2(0.0f, 0.0f),
				Vector2(static_cast<float>(swapChainDesc.Width), static_cast<float>(swapChainDesc.Height)),
				windowSetting.gameSizeFloat);
		}

		// エディタのフレーム開始処理
		editorManager_.BeginFrame(graphicsCore, editorContext_);
		HandleCloseRequestResult();
	}

	// 即時ライン描画は1フレームで消えるので、スクリプトが発行する前にクリアする
	LineImmediateBuffer::GetInstance().BeginFrame();

	// プレファブ編集中はこのフレームのUIで作られたエンティティをプレファブの一部へ取り込む
	// ECS更新の前に行い、親子付け後のワールド行列が同フレームで正しく計算されるようにする
	if (IsPrefabEditing()) {
		SyncPrefabEditedEntities();
	}

	// ECSシステムの更新
	if (ShouldAdvanceActiveWorld()) {

		FrameProfiler::ScopedSample ecsSample(FrameProfiler::Category::Ecs);
		// Play中にscript例外が出たらUnity風にEditへ戻すため、tick前後で例外storeのversionを比べる
		const bool playingThisTick = worldManager_.IsPlaying();
		const uint64_t sceneRevisionBeforeTick = playingThisTick ?
			playScenes_.GetRevision() : 0;
		const uint64_t scriptExceptionVersion = playingThisTick ?
			ManagedScriptExceptionStore::GetInstance().Version() : 0;
		auto& scriptProfiler = ScriptProfiler::GetInstance();
		scriptProfiler.BeginFrame();
		scheduler_.Tick(GetActiveWorld(), systemContext_);
		scriptProfiler.EndFrame();
		if (playingThisTick && sceneRevisionBeforeTick != playScenes_.GetRevision()) {

			// 同期シーン読み込みに使った時間を次のPlayフレームへ持ち越さない
			requestFrameDeltaReset_ = true;
			scriptProfiler.Configure(scriptProfiler.IsEnabled(), {}, 0);
		}
		if (playingThisTick && ManagedScriptExceptionStore::GetInstance().Version() != scriptExceptionVersion) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"EngineApplication: Play中のScript例外を検出したためEditへ戻ります");
			StopPlayWorld();
			world = systemContext_.world;
			header = systemContext_.activeSceneHeader;
		}
	}
	if (HandleApplicationQuitRequest()) {

		world = systemContext_.world;
		header = systemContext_.activeSceneHeader;
	}
	if (playSession_->IsFrameStepRequested()) {

		playSession_->FinishFrameStep();
		systemContext_.deltaTime = 0.0f;
	}

	// C++ツールの更新、ECSシステム更新の後に行うことで衝突判定可視化等が更新後のワールド行列を参照する
	if constexpr (BuildConfig::kEditorEnabled) {
		if (!editorManager_.GetLayoutState().hidePanels) {

			ToolContext toolContext{};
			toolContext.world = world;
			toolContext.assetDatabase = &assetDatabase_;
			toolContext.systemContext = &systemContext_;
			toolContext.sceneInstances = editorContext_.sceneInstances;
			toolContext.activeSceneHeader = header;
			toolContext.activeSceneAsset = editorContext_.activeSceneAsset;
			toolContext.activeSceneInstanceID = editorContext_.activeSceneInstanceID;
			toolContext.activeScenePath = activeScenePath_;
			toolContext.isPlaying = worldManager_.IsPlaying();
			toolContext.canEditScene = !worldManager_.IsPlaying() && world;
			toolContext.deltaTime = deltaTime;
			ToolRegistry::GetInstance().Tick(toolContext);
		}
	}
}

bool Engine::EngineApplication::ConsumeFrameDeltaResetRequest() {

	const bool requested = requestFrameDeltaReset_;
	requestFrameDeltaReset_ = false;
	return requested;
}

void Engine::EngineApplication::Render(GraphicsCore& graphicsCore) {

	// テクスチャアップロードなど、描画前に確定したいGPUサービスを更新する
	graphicsCore.TickFrameServices();

	// バックバッファ描画クリア
	graphicsCore.Render();

	if constexpr (BuildConfig::kEditorEnabled) {

		if (!editorManager_.GetLayoutState().hidePanels) {

			// SceneViewに重ねる選択エンティティのデバッグラインを、SceneView描画前に積む
			editorManager_.DrawSceneDebugObjects(editorContext_);
		}
	}

	// ワールドを描画
	renderPipeline_->Render(graphicsCore, BuildRenderFrameRequest(graphicsCore, GetActiveWorld(), GetActiveSceneHeader()));

	if constexpr (BuildConfig::kEditorEnabled) {

		const bool hidePanels = editorManager_.GetLayoutState().hidePanels;
		if (hidePanels) {

			// エディターUIを経由せず、Release時と同じGameViewの全画面表示にする
			renderPipeline_->PresentViewToBackBuffer(graphicsCore, RenderViewKind::Game);
		} else {

			// シーンビューのメッシュピック処理
			editorManager_.ExecuteSceneMeshPicking(graphicsCore, editorContext_, *renderPipeline_);
		}

		// エディタのフレーム終了処理
		editorManager_.EndFrame(graphicsCore, editorContext_, &renderPipeline_->GetViewportRenderService(),
			&renderPipeline_->GetResolvedView(RenderViewKind::Scene), renderPipeline_.get());
	} else {

		// エディタがない場合はゲームビューをバックバッファに描画する
		renderPipeline_->PresentViewToBackBuffer(graphicsCore, RenderViewKind::Game);
	}
}

void Engine::EngineApplication::RenderPlatformWindows([[maybe_unused]] GraphicsCore& graphicsCore) {

	if constexpr (BuildConfig::kEditorEnabled) {
		editorManager_.RenderPlatformWindows();
	}
}

int Engine::RunEditorApplication() {

	Framework framework(std::make_unique<EngineApplication>());
	framework.Run();
	return 0;
}

//============================================================================
//	EngineApplication classMethods
//============================================================================

void Engine::EngineApplication::EnterPrefabEdit(AssetID prefabAsset) {

	prefabSession_->EnterPrefabEdit(prefabAsset);
}

bool Engine::EngineApplication::ExitPrefabEdit() {

	return prefabSession_->ExitPrefabEdit();
}

void Engine::EngineApplication::ExitAllPrefabEdit() {

	prefabSession_->ExitAllPrefabEdit();
}

void Engine::EngineApplication::TogglePrefabInContextMode() {

	prefabSession_->TogglePrefabInContextMode();
}

bool Engine::EngineApplication::SaveCurrentPrefab() {

	return prefabSession_->SaveCurrentPrefab();
}

void Engine::EngineApplication::SyncPrefabEditedEntities() {

	prefabSession_->SyncPrefabEditedEntities();
}

ECSWorld* EngineApplication::GetActiveWorld() {

	return prefabSession_->GetActiveWorld();
}

SceneInstanceManager& EngineApplication::GetActiveScenes() {

	return prefabSession_->GetActiveScenes();
}

bool Engine::EngineApplication::IsPrefabEditing() const {

	return prefabSession_->IsEditing();
}

bool Engine::EngineApplication::SaveActiveEditScene() {

	return sceneSaveController_->SaveActiveEditScene();
}

bool Engine::EngineApplication::SaveAllEditScenes() {

	return sceneSaveController_->SaveAllEditScenes();
}

void Engine::EngineApplication::UpdateSceneSave() {

	sceneSaveController_->UpdateSceneSave();
}

bool Engine::EngineApplication::WaitForSceneSave() {

	return sceneSaveController_->WaitForSceneSave();
}

void Engine::EngineApplication::HandlePlayToggle() {

	playSession_->HandlePlayToggle();
}

void Engine::EngineApplication::StopPlayWorld() {

	playSession_->StopPlayWorld();
}

bool Engine::EngineApplication::HandleApplicationQuitRequest() {

	return playSession_->HandleApplicationQuitRequest();
}

void Engine::EngineApplication::StartPlayWorld() {

	playSession_->StartPlayWorld();
}

void Engine::EngineApplication::HandlePlayPauseRequests() {

	playSession_->HandlePlayPauseRequests();
}

bool Engine::EngineApplication::ShouldAdvanceActiveWorld() const {

	return playSession_->ShouldAdvanceActiveWorld();
}

Engine::RenderFrameRequest Engine::EngineApplication::BuildRenderFrameRequest(
	GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header) {

	return renderRequestBuilder_->BuildRenderFrameRequest(graphicsCore, world, header, GetActiveScenes());
}

Engine::EngineApplication::EngineApplication() {

	prefabSession_ = std::make_unique<PrefabEditSession>(assetDatabase_,
		worldManager_,
		editScenes_,
		playScenes_,
		scheduler_,
		systemContext_,
		editorManager_);
	sceneSaveController_ = std::make_unique<SceneSaveController>(assetDatabase_,
		worldManager_,
		editScenes_,
		sceneSystem_,
		editorManager_,
		activeScenePath_,
		[this]() { RestoreEditModeUIVisuals(); });
	playSession_ = std::make_unique<EditorPlaySession>(assetDatabase_,
		worldManager_,
		editScenes_,
		playScenes_,
		sceneSystem_,
		scheduler_,
		systemContext_,
		editorManager_,
		runtimeWorldBaker_,
		scriptBuildService_,
		requestFrameDeltaReset_,
		[this]() { return IsPrefabEditing(); },
		[this]() { return SaveAllEditScenes(); },
		[this]() { RefreshActiveWorldContext(); });
	renderRequestBuilder_ = std::make_unique<EditorRenderRequestBuilder>(systemContext_,
		assetDatabase_,
		editorManager_,
		worldManager_);
}

Engine::EngineApplication::~EngineApplication() = default;
