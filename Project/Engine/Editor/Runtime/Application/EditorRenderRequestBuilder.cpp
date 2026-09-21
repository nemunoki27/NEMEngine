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

using namespace Engine;

Engine::EditorRenderRequestBuilder::EditorRenderRequestBuilder(SystemContext& systemContext,
	AssetDatabase& assetDatabase,
	EditorManager& editorManager,
	WorldManager& worldManager) :
	systemContext_(systemContext),
	assetDatabase_(assetDatabase),
	editorManager_(editorManager),
	worldManager_(worldManager) {
}

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
		if (layout.hidePanels) {

			// HidePanels中はReleaseと同じくGameViewだけを描画対象にする
			showGameView = true;
			showSceneView = false;
		} else {

			showGameView = layout.showGameView;
			showSceneView = layout.showSceneView;
			sceneViewCameraSelection = editorManager_.GetSceneViewCameraSelection();
			manualSceneCamera = editorManager_.GetSceneViewCameraState();
			request.drawSceneViewDefaultGrid = editorManager_.ShouldDrawSceneViewDefaultGrid();
			request.drawSceneView2DCameraBounds = editorManager_.ShouldDrawSceneView2DCameraBounds();
		}

		// 2つのViewを表示中は操作対象を毎フレーム、副Viewを30Hzで更新してGPUの熱飽和を防ぐ
		renderGameView = showGameView;
		renderSceneView = showSceneView;
		if (showGameView && showSceneView && !layout.hidePanels) {

			const bool renderSecondaryView =
				(renderFrameSerial_ % 2) == 0;
			const EditorState& editorState =
				editorManager_.GetEditorState();
			if (worldManager_.IsPlaying()) {
				renderSceneView = renderSecondaryView ||
					editorState.sceneViewportHovered;
			} else {
				renderGameView = renderSecondaryView ||
					editorState.gameViewportHovered;
			}
		}
		++renderFrameSerial_;
	}
	// ゲームビューの要求を構築
	{
		RenderViewRequest& viewRequest = request.views[static_cast<uint32_t>(RenderViewKind::Game)];
		viewRequest.kind = RenderViewKind::Game;
		viewRequest.enabled = showGameView;
		viewRequest.renderThisFrame = renderGameView;
		viewRequest.width = showGameView ? fixedRenderWidth : 0;
		viewRequest.height = showGameView ? fixedRenderHeight : 0;
		viewRequest.sourceKind = RenderViewSourceKind::WorldCamera;
		viewRequest.preferredOrthographicCameraUUID = UUID{};
		viewRequest.preferredPerspectiveCameraUUID = UUID{};
	}
	// シーンビューの要求を構築
	{
		RenderViewRequest& viewRequest = request.views[static_cast<uint32_t>(RenderViewKind::Scene)];
		viewRequest.kind = RenderViewKind::Scene;
		viewRequest.enabled = showSceneView;
		viewRequest.renderThisFrame = renderSceneView;
		viewRequest.width = showSceneView ? fixedRenderWidth : 0;
		viewRequest.height = showSceneView ? fixedRenderHeight : 0;
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
