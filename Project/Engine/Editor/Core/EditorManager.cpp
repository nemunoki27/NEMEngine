#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Editor/Tools/Core/EditorToolUI.h>
#include <Engine/Editor/Commands/Entity/CreateEntityCommand.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/Core/SceneViewInteractionPolicy.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>

#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <algorithm>
#include <optional>

// imgui
#include <ImGuizmo.h>

//============================================================================
//	EditorManager classMethods
//============================================================================
namespace {

	// ドッキングスペースのホストウィンドウ名
	constexpr const char* kDockSpaceHostWindow = "##EditorDockSpaceHost";
	constexpr const char* kDockSpaceID = "EngineEditorDockSpace";
	bool IsHidePanelsShortcutTriggered() {

		Engine::Input* input = Engine::Input::GetInstance();
		const bool directInputDown = input && input->PushKey(DIK_TAB) && input->PushKey(DIK_ESCAPE);

		const bool imguiDown = ImGui::IsKeyDown(ImGuiKey_Tab) && ImGui::IsKeyDown(ImGuiKey_Escape);

		const bool shortcutDown = directInputDown || imguiDown;

		// 同時押しの開始時だけ切り替える
		static bool wasShortcutDown = false;
		const bool triggered = shortcutDown && !wasShortcutDown;
		wasShortcutDown = shortcutDown;
		return triggered;
	}

}

void Engine::EditorManager::HandleGlobalShortcuts(const EditorContext& context) {

	if (editorCommandPanelKind_ != EditorCommandPanelKind::Scene) {
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput || ImGui::IsAnyItemActive()) {
		return;
	}

	// 処理を戻す
	if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) {

		UndoEditorCommand();
		return;
	}
	// 処理を進める
	if ((io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))) {

		RedoEditorCommand();
		return;
	}
	// 複製
	if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D)) {

		DuplicateSelection();
		return;
	}
	// 編集中のSceneまたはPrefabを保存する
	if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {

		if (context.isPrefabEditing) {
			RequestSavePrefab();
		} else {
			RequestSaveScene();
		}
		return;
	}
	// コピー
	if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C)) {

		CopySelectionToClipboardInternal(context);
		return;
	}
	// 貼り付け
	if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V)) {

		PasteClipboard();
		return;
	}
	// 選択範囲を一括削除する
	if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
		DeleteSelection();
		return;
	}
	// ギズモ操作のショートカット
	{
		// Scene操作のフォーカス中に切り替える

		// 座標移動
		if (ImGui::IsKeyPressed(ImGuiKey_T)) {

			editorState_.sceneViewManipulatorMode = SceneViewManipulatorMode::Translate;
			return;
		}
		// 回転
		if (ImGui::IsKeyPressed(ImGuiKey_R)) {

			editorState_.sceneViewManipulatorMode = SceneViewManipulatorMode::Rotate;
			return;
		}
		// 拡縮
		if (ImGui::IsKeyPressed(ImGuiKey_S)) {

			editorState_.sceneViewManipulatorMode = SceneViewManipulatorMode::Scale;
			return;
		}
		// 選択のみ
		if (ImGui::IsKeyPressed(ImGuiKey_H)) {

			editorState_.sceneViewManipulatorMode = SceneViewManipulatorMode::None;
			return;
		}
		// グリッド操作切り替え
		if (ImGui::IsKeyPressed(ImGuiKey_G)) {

			editorState_.enableSnapEditEntity = !editorState_.enableSnapEditEntity;
			return;
		}
		// エンティティ選択単位の切り替え
		if (ImGui::IsKeyPressed(ImGuiKey_E)) {
			// エンティティ選択中ならサブメッシュ選択中に切り替える
			if (editorState_.selectKind == EditorSelectionKind::Entity) {

				editorState_.selectKind = EditorSelectionKind::MeshSubMesh;
				return;
			} else if (editorState_.selectKind == EditorSelectionKind::MeshSubMesh) {

				editorState_.selectKind = EditorSelectionKind::Entity;
				return;
			}
		}
	}
}

void Engine::EditorManager::BeginFrame(GraphicsCore& graphicsCore, const EditorContext& context) {

	if (!initialized_) {
		return;
	}

	gameBuildSession_->Update();

	// 現在のレンダリングコンテキストを保存
	currentRenderContext_ = &context;

	// 前フレームのパネル操作をImGuiフレーム開始前に反映する
	RemoveClosedDuplicatedPanels();
	ApplyPendingEditorLayout(graphicsCore);

	// フレーム開始
	imguiManager_.Begin();
	if (!layoutState_.hidePanels && IsHidePanelsShortcutTriggered()) {

		// Panelを隠して製品サイズへ切り替える
		layoutState_.hidePanels = true;
		EndPanelPreviews();
		WinApp::BeginProductSizePreview(EngineContext::GetWindowSetting().gameSize);
		return;
	}
	if (layoutState_.hidePanels) {
		EndPanelPreviews();

		// Panel非表示中は復帰入力だけを処理する
		if (IsHidePanelsShortcutTriggered()) {
			layoutState_.hidePanels = false;
			WinApp::EndProductSizePreview();
		} else {
			WinApp::BeginProductSizePreview(EngineContext::GetWindowSetting().gameSize);
			return;
		}
	}

	// シーンビューのメッシュピック処理の結果を選択状態へ適用する
	const MeshSubMeshPickOutcome pickOutcome = meshSubMeshPicker_->ConsumePendingResult(graphicsCore, context.activeWorld);
	if (pickOutcome.resolved && pickOutcome.requestID == editorState_.scenePickRequestID) {

		// 最新クリックの結果だけ候補へ反映し、リリース済みなら選択を確定する
		const bool dimensionAllowed =
			pickOutcome.hit && context.activeWorld &&
			IsScenePickDimensionAllowed(*context.activeWorld, pickOutcome.entity, editorState_.sceneViewPickDimension);
		editorState_.scenePickDragEntity = dimensionAllowed ? pickOutcome.entity : Entity::Null();
		editorState_.scenePickCandidateRequestID = pickOutcome.requestID;
		if (dimensionAllowed) {
			editorState_.scenePickCandidateSubMesh = pickOutcome.subMeshIndex;
			editorState_.scenePickCandidateSubMeshID = pickOutcome.subMeshStableID;
		} else {
			editorState_.scenePickCandidateSubMesh = 0;
			editorState_.scenePickCandidateSubMeshID = UUID{};
		}
		if (context.activeWorld && editorState_.scenePickClickPending) {
			editorState_.CommitScenePick(*context.activeWorld);
		}
	}

	editorState_.ValidateSelection(context.activeWorld);

	ImGuizmo::BeginFrame();
	DrawDockSpace();

	// 3D対象へのフォーカス要求を取り出す
	if (sceneViewCameraController_ && editorState_.cameraFocusRequest.IsValid()) {

		ECSWorld* focusWorld = context.activeWorld;
		const Entity focusTarget = editorState_.cameraFocusRequest;
		editorState_.cameraFocusRequest = Entity::Null();
		const std::optional<Dimension> focusDimension =
			focusWorld ? ResolveEntityDimension(*focusWorld, focusTarget) : std::nullopt;
		if (focusDimension && *focusDimension == Dimension::Type3D &&
			ResolveSceneViewCameraDimension(editorState_.sceneViewPickDimension) == Dimension::Type3D) {

			sceneViewCameraController_->FocusOn(
				RenderItemExtract::GetWorldMatrix(*focusWorld, focusTarget).GetTranslationValue());
		}
	}
	// フォーカス移動を進める
	if (sceneViewCameraController_) {

		sceneViewCameraController_->UpdateFocus();
		// フォーカス中はギズモ操作を無効にして誤移動を防ぐ
		editorState_.cameraFocusing = sceneViewCameraController_->IsFocusing();
	}

	// シーンビューのマニュアルカメラを更新
	UpdateSceneViewManualCamera();

	// 各パネルの描画
	editorCommandPanelKind_ = EditorCommandPanelKind::None;
	EditorPanelContext panelContext = CreatePanelContext(graphicsCore, context);
	panelContext.viewportRenderService = nullptr;

	DrawPanelsByPhase(panelContext, EditorPanelPhase::PreScene);
	requests_.DrawUnsavedScenePopup();
	requests_.DrawCloseUnsavedScenePopup();
	requests_.DrawSceneSaveConflictPopup();
}

void Engine::EditorManager::DrawSceneDebugObjects([[maybe_unused]] const EditorContext& context) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (!initialized_ || layoutState_.hidePanels || !layoutState_.showSceneView || !context.activeWorld) {
		return;
	}

	// 表示対象のスナップグリッドを描く
	const SceneViewSnapGridDecision gridDecision = ResolveSceneViewSnapGridDecision(editorState_, context.activeWorld);
	if (gridDecision.visible) {

		if (gridDecision.use2D) {
			LineRenderer::GetInstance()->Get2D()->DrawGrid(gridDecision.cellSize);
		} else {
			LineRenderer::GetInstance()->Get3D()->DrawGrid(gridDecision.cellSize);
		}
	}

	if (!editorState_.HasValidSelection(context.activeWorld)) {
		return;
	}

	// 選択中のSubMesh番号を解決する
	int32_t selectionSubMeshIndex = -1;
	uint32_t resolvedSubMeshIndex = 0;
	if (editorState_.HasValidSubMeshSelection(context.activeWorld) &&
		editorState_.TryResolveSelectedSubMeshIndex(context.activeWorld, resolvedSubMeshIndex)) {
		selectionSubMeshIndex = static_cast<int32_t>(resolvedSubMeshIndex);
	}
	// 選択範囲の補助描画をまとめる
	const std::vector<Entity>& selectedEntities = editorState_.GetSelectedEntities();
	if (selectedEntities.size() <= 1) {
		InspectorDrawerCommon::DrawEntityDebugObject(*context.activeWorld, editorState_.selectedEntity, selectionSubMeshIndex);
	} else {
		for (const Entity& selected : selectedEntities) {
			const int32_t subMesh = (selected == editorState_.selectedEntity) ? selectionSubMeshIndex : -1;
			InspectorDrawerCommon::DrawEntityDebugObject(*context.activeWorld, selected, subMesh);
		}
	}

#endif
}

void Engine::EditorManager::EndFrame(GraphicsCore& graphicsCore, const EditorContext& context,
	const ViewportRenderService* viewportRenderService, const ResolvedRenderView* sceneRenderView,
	RenderPipelineRunner* renderPipeline) {

	if (!initialized_) {
		return;
	}

	if (layoutState_.hidePanels) {

		// Panel非表示でもImGuiの入力更新を閉じる
		imguiManager_.End();
		currentRenderContext_ = nullptr;
		return;
	}

	// 各パネルの描画
	EditorPanelContext panelContext = CreatePanelContext(graphicsCore, context);
	panelContext.viewportRenderService = viewportRenderService;
	panelContext.renderPipeline = renderPipeline;
	panelContext.sceneRenderView = sceneRenderView;
	panelContext.sceneViewCamera = GetSceneViewCameraState();

	// ドッキングスペースの描画
	DrawPanelsByPhase(panelContext, EditorPanelPhase::PostScene);

	// メニューを閉じても独立ツールを描画する
	EditorToolUI::DrawWindows(panelContext);

	// パネルとツールのフォーカスが確定してからメイン編集コマンドを処理
	HandleGlobalShortcuts(context);
	ApplyPendingPanelDuplicate(panelContext);

	imguiManager_.End();
	if (ImGui::GetIO().WantSaveIniSettings) {

		editorLayoutManager_.SaveSession(CaptureEditorLayout());
		ImGui::GetIO().WantSaveIniSettings = false;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	const DXGI_SWAP_CHAIN_DESC1& swapChainDesc = graphicsCore.GetSwapChainDesc();

	dxCommand->BindRenderTargets(std::optional<RenderTarget>(graphicsCore.GetBackBufferRenderTarget()),
		graphicsCore.GetDSVDescriptor().GetFrameCPUHandle());

	dxCommand->SetViewportAndScissor(swapChainDesc.Width, swapChainDesc.Height);
	dxCommand->SetDescriptorHeaps({graphicsCore.GetSRVDescriptor().GetDescriptorHeap()});

	imguiManager_.Draw(dxCommand->GetCommandList());

	currentRenderContext_ = nullptr;
}

void Engine::EditorManager::RenderPlatformWindows() {

	if (!initialized_) {
		return;
	}
	imguiManager_.DrawPlatformWindows();
}

void Engine::EditorManager::DrawDockSpace() {

	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	// ドッキングスペースのホストウィンドウのフラグ
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
								   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
								   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	bool open = true;
	ImGui::Begin(kDockSpaceHostWindow, &open, windowFlags);
	ImGui::PopStyleVar(3);

	const ImGuiID dockSpaceID = ImGui::GetID(kDockSpaceID);
	if (requestBuildDefaultDockLayout_) {
		BuildDefaultDockLayout(dockSpaceID, viewport->WorkSize);
		requestBuildDefaultDockLayout_ = false;
	}
	ImGui::DockSpace(dockSpaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	ImGui::End();
}

void Engine::EditorManager::BuildDefaultDockLayout(ImGuiID dockSpaceID, const ImVec2& dockSpaceSize) {

	ImGui::DockBuilderRemoveNode(dockSpaceID);
	ImGui::DockBuilderAddNode(dockSpaceID, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockSpaceID, dockSpaceSize);

	ImGuiID mainDockID = dockSpaceID;
	ImGuiID toolbarDockID = 0;
	ImGui::DockBuilderSplitNode(mainDockID, ImGuiDir_Up, 0.035f, &toolbarDockID, &mainDockID);

	ImGuiID inspectorDockID = 0;
	ImGui::DockBuilderSplitNode(mainDockID, ImGuiDir_Right, 0.44f, &inspectorDockID, &mainDockID);

	ImGuiID bottomDockID = 0;
	ImGui::DockBuilderSplitNode(mainDockID, ImGuiDir_Down, 0.46f, &bottomDockID, &mainDockID);

	ImGuiID hierarchyDockID = 0;
	ImGui::DockBuilderSplitNode(mainDockID, ImGuiDir_Left, 0.17f, &hierarchyDockID, &mainDockID);

	ImGuiID consoleDockID = 0;
	ImGui::DockBuilderSplitNode(bottomDockID, ImGuiDir_Left, 0.24f, &consoleDockID, &bottomDockID);

	ImGui::DockBuilderDockWindow("Toolbar", toolbarDockID);
	ImGui::DockBuilderDockWindow("Hierarchy", hierarchyDockID);
	ImGui::DockBuilderDockWindow("Inspector###Inspector:inspector.primary", inspectorDockID);
	ImGui::DockBuilderDockWindow("Project###Project:project.primary", bottomDockID);
	ImGui::DockBuilderDockWindow("Console", consoleDockID);
	ImGui::DockBuilderDockWindow("SceneView", mainDockID);
	ImGui::DockBuilderDockWindow("GameView", mainDockID);
	ImGui::DockBuilderFinish(dockSpaceID);
}

Engine::EditorPanelContext Engine::EditorManager::CreatePanelContext(GraphicsCore& graphicsCore, const EditorContext& context) {

	// Frame内だけで使うPanelの接続を揃える
	EditorPanelContext panelContext{};
	panelContext.editorContext = &context;
	panelContext.editorState = &editorState_;
	panelContext.layoutState = &layoutState_;
	panelContext.host = this;
	panelContext.gameBuildSession = gameBuildSession_.get();
	panelContext.tagSettings = &tagSettings_;
	panelContext.renderingLayerSettings = &renderingLayerSettings_;
	panelContext.graphicsCore = &graphicsCore;
	panelContext.graphicsPlatform = &graphicsCore.GetDXObject();
	return panelContext;
}
