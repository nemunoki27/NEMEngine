#include "EditorRenderRequestBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Editor/Core/EditorManager.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>

using namespace Engine;

Engine::EditorRenderRequestBuilder::EditorRenderRequestBuilder(SystemContext& systemContext,
	AssetDatabase& assetDatabase,
	EditorManager& editorManager,
	WorldManager& worldManager) :
	systemContext_(systemContext),
	assetDatabase_(assetDatabase),
	editorManager_(editorManager),
	worldManager_(worldManager) {}

Engine::RenderFrameRequest Engine::EditorRenderRequestBuilder::BuildRenderFrameRequest(
	GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header, SceneInstanceManager& scenes) {

	// エディタの状態を描画要求へ変換する
	RenderFrameRequest request{};
	request.header = header;
	request.world = world;
	// 描画側がECSシステムと同じフレーム情報を参照できるように渡す
	request.systemContext = &systemContext_;
	request.assetDatabase = &assetDatabase_;

	// Play->プレファブ編集->Editの順でシーンインスタンスを切り替える
	SceneInstanceManager* activeScenes = &scenes;
	const SceneInstance* activeInstance = activeScenes->GetActive();
	request.sceneInstances = activeScenes;
	request.activeSceneInstanceID = activeInstance ? activeInstance->instanceID : UUID{};

	const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
	// GameView/SceneViewは同じ固定解像度を基準に描画サーフェイスを作る
	uint32_t fixedRenderWidth = windowSetting.gameSize.x;
	uint32_t fixedRenderHeight = windowSetting.gameSize.y;

	// エディタの状態に応じて描画ビューの要求を構築する
	bool showGameView = true;
	bool showSceneView = false;
	bool renderGameView = true;
	bool renderSceneView = false;
	SceneViewCameraSelection sceneViewCameraSelection{};
	ManualRenderCameraState manualSceneCamera{};

	// エディタが有効な場合はエディタのレイアウト状態に応じてビューの要求を構築する
	if constexpr (BuildConfig::kEditorEnabled) {

		const EditorLayoutState& layout = editorManager_.GetLayoutState();
		const EditorState& editorState = editorManager_.GetEditorState();
		// 非表示中も同じCameraの履歴と描画資源を保持する
		sceneViewCameraSelection = editorManager_.GetSceneViewCameraSelection();
		manualSceneCamera = editorManager_.GetSceneViewCameraState();
		if (layout.hidePanels) {

			// HidePanels中はReleaseと同じくGameViewだけを描画対象にする
			showGameView = true;
			showSceneView = false;
		} else {

			showGameView = layout.showGameView && editorState.gameViewportVisible;
			showSceneView = layout.showSceneView && editorState.sceneViewportVisible;
			request.drawSceneViewDefaultGrid = showSceneView &&
				editorManager_.ShouldDrawSceneViewDefaultGrid();
			request.drawSceneView2DCameraBounds = showSceneView &&
				editorManager_.ShouldDrawSceneView2DCameraBounds();
		}

		// 操作対象は毎frame、副Viewは負荷に応じて更新する
		renderGameView = showGameView;
		renderSceneView = showSceneView;
		if (renderGameView && renderSceneView && !layout.hidePanels) {

			const bool renderSecondaryView = ShouldRenderSecondaryView();
			const bool gameViewPrimary = editorState.gameViewportHovered ||
				(worldManager_.IsPlaying() && !editorState.sceneViewportHovered);
			if (gameViewPrimary) {
				renderSceneView = renderSecondaryView;
			} else {
				renderGameView = renderSecondaryView;
			}
		}
	}
	DisplayOutputSettings gameOutput = graphicsCore.GetDisplayOutputSettings();
	DisplayOutputSettings sceneOutput = gameOutput;
	if constexpr (BuildConfig::kEditorEnabled) {
		if (!editorManager_.GetLayoutState().hidePanels) {
			gameOutput = editorManager_.GetViewDisplayOutput(RenderViewKind::Game);
			sceneOutput = editorManager_.GetViewDisplayOutput(RenderViewKind::Scene);
		}
	}
	// 出力変更時は副Viewもそのフレームで更新する
	if (gameOutput != previousGameOutput_) renderGameView = showGameView;
	if (sceneOutput != previousSceneOutput_) renderSceneView = showSceneView;
	previousGameOutput_ = gameOutput;
	previousSceneOutput_ = sceneOutput;

	// ゲームビューの要求を構築
	{
		RenderViewRequest& viewRequest = request.views[static_cast<uint32_t>(RenderViewKind::Game)];
		viewRequest.kind = RenderViewKind::Game;
		viewRequest.displayOutput = gameOutput;
		// Camera情報とRenderTexture出力はPanel非表示中も維持する
		viewRequest.enabled = true;
		viewRequest.renderThisFrame = true;
		viewRequest.renderDefaultSurfaceThisFrame = renderGameView;
		viewRequest.width = fixedRenderWidth;
		viewRequest.height = fixedRenderHeight;
		viewRequest.sourceKind = RenderViewSourceKind::WorldCamera;
		viewRequest.preferredOrthographicCameraUUID = UUID{};
		viewRequest.preferredPerspectiveCameraUUID = UUID{};
	}
	// シーンビューの要求を構築
	{
		RenderViewRequest& viewRequest = request.views[static_cast<uint32_t>(RenderViewKind::Scene)];
		viewRequest.kind = RenderViewKind::Scene;
		viewRequest.displayOutput = sceneOutput;
		viewRequest.enabled = BuildConfig::kEditorEnabled;
		viewRequest.renderThisFrame = renderSceneView;
		viewRequest.renderDefaultSurfaceThisFrame = renderSceneView;
		viewRequest.width = viewRequest.enabled ? fixedRenderWidth : 0;
		viewRequest.height = viewRequest.enabled ? fixedRenderHeight : 0;
		viewRequest.manualCamera = manualSceneCamera;

		// Entity Cameraが指定されている場合だけWorld側のカメラを使う
		if (sceneViewCameraSelection.mode == SceneViewCameraMode::SelectedEntityCamera &&
			sceneViewCameraSelection.HasAnyAssignedCamera()) {

			viewRequest.sourceKind = RenderViewSourceKind::WorldCamera;
			viewRequest.preferredOrthographicCameraUUID = sceneViewCameraSelection.orthographicCameraUUID;
			viewRequest.preferredPerspectiveCameraUUID = sceneViewCameraSelection.perspectiveCameraUUID;
		} else {

			// 通常はエディタ用の手動カメラを使う
			viewRequest.sourceKind = RenderViewSourceKind::ManualCamera;
			viewRequest.preferredOrthographicCameraUUID = UUID{};
			viewRequest.preferredPerspectiveCameraUUID = UUID{};
		}
	}
	return request;
}

bool Engine::EditorRenderRequestBuilder::ShouldRenderSecondaryView() {

	const auto now = std::chrono::steady_clock::now();
	const auto& profiler = FrameProfiler::GetInstance();
	if (profiler.HasGPUData() && profiler.GetGPUFrameID() != lastGPUFrameID_) {

		// 同じ計測結果を重複して平均へ入れない
		lastGPUFrameID_ = profiler.GetGPUFrameID();
		const float gpuTime = profiler.GetGPUTotalMs();
		averagedGPUTime_ = averagedGPUTime_ > 0.0f ? averagedGPUTime_ + (gpuTime - averagedGPUTime_) * 0.05f : gpuTime;
		const uint32_t targetFPS = FrameRateSettings::GetInstance().GetPresentTargetFps();
		const float frameBudget = 1000.0f / (targetFPS > 0 ? targetFPS : 60u);
		if (averagedGPUTime_ > frameBudget * 0.85f) {

			// 描画が重い間は副Viewを15Hzへ落とす
			secondaryViewThrottled_ = true;
			secondaryRecoveryStarted_ = {};
		} else if (secondaryViewThrottled_ && averagedGPUTime_ < frameBudget * 0.5f) {

			// 余裕が続いてから30Hzへ戻す
			if (secondaryRecoveryStarted_ == std::chrono::steady_clock::time_point{}) secondaryRecoveryStarted_ = now;
			if (now - secondaryRecoveryStarted_ >= std::chrono::seconds(5)) secondaryViewThrottled_ = false;
		} else {
			secondaryRecoveryStarted_ = {};
		}
	}
	if (now < nextSecondaryViewUpdate_) return false;

	// 遅れた更新をまとめて実行しない
	const auto interval = std::chrono::duration<double>(1.0 / (secondaryViewThrottled_ ? 15.0 : 30.0));
	const auto updateInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);
	if (nextSecondaryViewUpdate_ == std::chrono::steady_clock::time_point{} || now - nextSecondaryViewUpdate_ >= updateInterval) {
		nextSecondaryViewUpdate_ = now + updateInterval;
	} else {
		nextSecondaryViewUpdate_ += updateInterval;
	}
	return true;
}
