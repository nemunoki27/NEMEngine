#include "EditorRenderRequestBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Editor/Core/EditorManager.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>

using namespace Engine;

Engine::EditorRenderRequestBuilder::EditorRenderRequestBuilder(SystemContext& systemContext,
	AssetDatabase& assetDatabase,
	EditorManager& editorManager) :
	systemContext_(systemContext),
	assetDatabase_(assetDatabase),
	editorManager_(editorManager) {}

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
	// 製品解像度は投影と入力の基準として保つ
	uint32_t fixedRenderWidth = windowSetting.gameSize.x;
	uint32_t fixedRenderHeight = windowSetting.gameSize.y;
	Vector2I gameRenderSize = windowSetting.gameSize;
	Vector2I sceneRenderSize = windowSetting.gameSize;

	// エディタの状態に応じて描画ビューの要求を構築する
	bool showGameView = true;
	bool showSceneView = false;
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

			if (editorState.fitViewportResolution) {
				gameRenderSize = editorState.gameViewportSizing.Resolve(windowSetting.gameSize);
				sceneRenderSize = editorState.sceneViewportSizing.Resolve(windowSetting.gameSize);
			}
			showGameView = layout.showGameView && editorState.gameViewportVisible;
			showSceneView = layout.showSceneView && editorState.sceneViewportVisible;
			request.drawSceneViewDefaultGrid = showSceneView &&
				editorManager_.ShouldDrawSceneViewDefaultGrid();
			request.drawSceneView2DCameraBounds = showSceneView &&
				editorManager_.ShouldDrawSceneView2DCameraBounds();
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

	// ゲームビューの要求を構築
	{
		RenderViewRequest& viewRequest = request.views[static_cast<uint32_t>(RenderViewKind::Game)];
		viewRequest.kind = RenderViewKind::Game;
		viewRequest.displayOutput = gameOutput;
		// Camera情報とRenderTexture出力はPanel非表示中も維持する
		viewRequest.enabled = true;
		viewRequest.renderThisFrame = true;
		viewRequest.renderDefaultSurfaceThisFrame = showGameView;
		viewRequest.width = gameRenderSize.x;
		viewRequest.height = gameRenderSize.y;
		viewRequest.referenceWidth = fixedRenderWidth;
		viewRequest.referenceHeight = fixedRenderHeight;
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
		viewRequest.renderThisFrame = showSceneView;
		viewRequest.renderDefaultSurfaceThisFrame = showSceneView;
		viewRequest.width = viewRequest.enabled ? sceneRenderSize.x : 0;
		viewRequest.height = viewRequest.enabled ? sceneRenderSize.y : 0;
		viewRequest.referenceWidth = fixedRenderWidth;
		viewRequest.referenceHeight = fixedRenderHeight;
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
