#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Core/Graphics/Render/View/ViewportRenderService.h>
#include <Engine/Core/Graphics/Render/View/SceneViewCameraController.h>
#include <Engine/Core/Graphics/Render/RenderPipelineRunner.h>
#include <Engine/Core/Graphics/Line/LineRenderer.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Editor/Command/EditorEntityDuplicateUtility.h>
#include <Engine/Editor/Command/Methods/CreateEntityCommand.h>
#include <Engine/Editor/Command/Methods/DeleteEntityCommand.h>
#include <Engine/Editor/Command/Methods/DuplicateEntityCommand.h>
#include <Engine/Editor/Command/Methods/PasteEntityTreeCommand.h>
#include <Engine/Input/Input.h>

// パネル群
#include <Engine/Editor/Panel/Methods/MenuBarPanel.h>
#include <Engine/Editor/Panel/Methods/ToolbarPanel.h>
#include <Engine/Editor/Panel/Methods/HierarchyPanel.h>
#include <Engine/Editor/Panel/Methods/InspectorPanel.h>
#include <Engine/Editor/Panel/Methods/ProjectPanel.h>
#include <Engine/Editor/Panel/Methods/ConsolePanel.h>
#include <Engine/Editor/Panel/Methods/ViewportPanel.h>
#include <Engine/Editor/Panel/Methods/SceneViewToolPanel.h>
#include <Engine/Editor/Panel/Methods/ToolPanel.h>

// imgui
#include <ImGuizmo.h>

//============================================================================
//	EditorManager classMethods
//============================================================================

namespace {

	// ドッキングスペースのホストウィンドウ名
	constexpr const char* kDockSpaceHostWindow = "##EditorDockSpaceHost";
	constexpr const char* kDockSpaceID = "EngineEditorDockSpace";
	constexpr const char* kUnsavedScenePopupName = "Unsaved Scene";
	// ImGuiのレイアウト保存ファイルパス
	constexpr const char* kEditorLayoutIniPath = "EditorLayout.ini";
}

void Engine::EditorManager::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	auto& engineContext = graphicsCore.GetContext();
	auto& graphicsPlatform = graphicsCore.GetDXObject();
	const auto& graphicsSetting = engineContext.GetGraphicsSetting();

	// ImGuiの初期化
	imguiManager_.Init(engineContext.GetWinApp()->GetHwnd(), graphicsCore.GetSwapChainDesc().BufferCount,
		graphicsPlatform.GetDevice(), graphicsPlatform.GetDxCommand()->GetQueue(),
		&graphicsCore.GetSRVDescriptor(), graphicsSetting.swapChainFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);

	// ImGuizmoのImGuiコンテキストを設定
	ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

	// ImGuiのレイアウト保存先
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = kEditorLayoutIniPath;

	// レイアウト構築フラグをリセット
	initialized_ = true;
	requestTogglePlay_ = false;
	sceneRequest_ = {};
	pendingSceneRequest_ = {};
	requestOpenUnsavedPopup_ = false;
	activeSceneDirty_ = false;

	// シーンビュー用のエディタカメラの状態を初期化
	sceneViewCameraState_ = SceneViewCameraController::MakeDefaultState();

	// 各パネルの生成と登録
	panels_.emplace_back(std::make_unique<MenuBarPanel>());
	panels_.emplace_back(std::make_unique<ToolbarPanel>());
	panels_.emplace_back(std::make_unique<HierarchyPanel>(graphicsCore.GetTextureUploadService()));
	panels_.emplace_back(std::make_unique<InspectorPanel>());
	panels_.emplace_back(std::make_unique<ConsolePanel>());
	panels_.emplace_back(std::make_unique<ToolPanel>());
	panels_.emplace_back(std::make_unique<ProjectPanel>(graphicsCore.GetTextureUploadService()));
	panels_.emplace_back(std::make_unique<ViewportPanel>("GameView", "GameView", ViewportPanelKind::Game));
	panels_.emplace_back(std::make_unique<ViewportPanel>("SceneView", "SceneView", ViewportPanelKind::Scene));
	panels_.emplace_back(std::make_unique<SceneViewToolPanel>(graphicsCore.GetTextureUploadService()));

	// シーンビューのメッシュピック処理の初期化
	meshSubMeshPicker_ = std::make_unique<MeshSubMeshPicker>();
	meshSubMeshPicker_->Init(graphicsCore);
}

Engine::EditorCommandContext Engine::EditorManager::MakeCommandContext(const EditorContext& context) {

	EditorCommandContext commandContext{};
	commandContext.editorContext = &context;
	commandContext.editorState = &editorState_;
	return commandContext;
}

bool Engine::EditorManager::ExecuteEditorCommand(std::unique_ptr<IEditorCommand> command) {

	if (!currentRenderContext_) {
		return false;
	}

	EditorCommandContext commandContext = MakeCommandContext(*currentRenderContext_);
	bool executed = editorState_.commandHistory.Execute(std::move(command), commandContext);
	if (executed) {
		activeSceneDirty_ = true;
	}
	return executed;
}

bool Engine::EditorManager::UndoEditorCommand() {

	if (!currentRenderContext_) {
		return false;
	}

	EditorCommandContext commandContext = MakeCommandContext(*currentRenderContext_);
	bool executed = editorState_.commandHistory.Undo(commandContext);
	if (executed) {
		activeSceneDirty_ = true;
	}
	return executed;
}

bool Engine::EditorManager::RedoEditorCommand() {

	if (!currentRenderContext_) {
		return false;
	}

	EditorCommandContext commandContext = MakeCommandContext(*currentRenderContext_);
	bool executed = editorState_.commandHistory.Redo(commandContext);
	if (executed) {
		activeSceneDirty_ = true;
	}
	return executed;
}

bool Engine::EditorManager::DuplicateSelection() {

	if (!currentRenderContext_) {
		return false;
	}
	if (!editorState_.HasValidSelection(currentRenderContext_->activeWorld) || currentRenderContext_->isPlaying) {
		return false;
	}
	return ExecuteEditorCommand(std::make_unique<DuplicateEntityCommand>(editorState_.selectedEntity));
}

bool Engine::EditorManager::CopySelectionToClipboardInternal(const EditorContext& context) {

	if (!editorState_.HasValidSelection(context.activeWorld) || context.isPlaying) {
		return false;
	}

	ECSWorld& world = *context.activeWorld;
	const Entity selected = editorState_.selectedEntity;

	// 選択しているエンティティとその子孫をスナップショットに保存する
	EditorEntitySnapshotUtility::CaptureSubtree(world, selected, editorState_.clipboardSnapshot);
	if (editorState_.clipboardSnapshot.IsEmpty()) {
		return false;
	}

	// 選択しているエンティティの親のUUIDを保存
	editorState_.clipboardParentStableUUID = UUID{};
	if (world.HasComponent<HierarchyComponent>(selected)) {

		const auto& hierarchy = world.GetComponent<HierarchyComponent>(selected);
		if (world.IsAlive(hierarchy.parent)) {
			editorState_.clipboardParentStableUUID = world.GetUUID(hierarchy.parent);
		}
	}

	// クリップボードは外部親を持たない独立スナップショットにしておく
	EditorEntityDuplicateUtility::ClearRootParentLink(editorState_.clipboardSnapshot);
	return true;
}

bool Engine::EditorManager::CopySelectionToClipboard() {

	if (!currentRenderContext_) {
		return false;
	}
	return CopySelectionToClipboardInternal(*currentRenderContext_);
}

bool Engine::EditorManager::PasteClipboard() {

	if (!currentRenderContext_) {
		return false;
	}
	if (currentRenderContext_->isPlaying || !editorState_.HasClipboard()) {
		return false;
	}
	// クリップボードの内容をシーンに貼り付けるコマンドを実行する
	return ExecuteEditorCommand(std::make_unique<PasteEntityTreeCommand>(
		editorState_.clipboardSnapshot, editorState_.clipboardParentStableUUID));
}

void Engine::EditorManager::RequestPlayToggle() {

	// プレイ要求フラグを立てる
	requestTogglePlay_ = true;
}

void Engine::EditorManager::RequestNewScene() {

	QueueSceneRequest({ EditorSceneRequestType::NewScene, AssetID{} });
}

void Engine::EditorManager::RequestOpenScene(AssetID sceneAsset) {

	if (!sceneAsset) {
		return;
	}
	QueueSceneRequest({ EditorSceneRequestType::OpenScene, sceneAsset });
}

void Engine::EditorManager::RequestSaveScene() {

	sceneRequest_ = { EditorSceneRequestType::SaveScene, AssetID{} };
}

void Engine::EditorManager::QueueSceneRequest(const EditorSceneRequest& request) {

	if (request.type == EditorSceneRequestType::NewScene ||
		request.type == EditorSceneRequestType::OpenScene) {

		if (IsActiveSceneDirty()) {

			pendingSceneRequest_ = request;
			requestOpenUnsavedPopup_ = true;
			return;
		}
	}
	sceneRequest_ = request;
}

const char* Engine::EditorManager::GetSceneRequestActionName(EditorSceneRequestType type) const {

	switch (type) {
	case EditorSceneRequestType::NewScene:
		return "creating a new scene";
	case EditorSceneRequestType::OpenScene:
		return "opening another scene";
	default:
		return "changing the scene";
	}
}

void Engine::EditorManager::SubmitPendingSceneRequest(bool saveBeforeSubmit) {

	if (pendingSceneRequest_.type == EditorSceneRequestType::None) {
		return;
	}

	if (saveBeforeSubmit) {

		switch (pendingSceneRequest_.type) {
		case EditorSceneRequestType::NewScene:
			sceneRequest_ = { EditorSceneRequestType::SaveAndNewScene, AssetID{} };
			break;
		case EditorSceneRequestType::OpenScene:
			sceneRequest_ = { EditorSceneRequestType::SaveAndOpenScene, pendingSceneRequest_.sceneAsset };
			break;
		default:
			sceneRequest_ = pendingSceneRequest_;
			break;
		}
	} else {

		sceneRequest_ = pendingSceneRequest_;
	}
	pendingSceneRequest_ = {};
}

void Engine::EditorManager::DrawUnsavedScenePopup() {

	if (requestOpenUnsavedPopup_) {

		ImGui::OpenPopup(kUnsavedScenePopupName);
		requestOpenUnsavedPopup_ = false;
	}

	if (!ImGui::BeginPopupModal(kUnsavedScenePopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextUnformatted("The current scene has unsaved changes.");
	ImGui::Text("Do you want to save before %s?", GetSceneRequestActionName(pendingSceneRequest_.type));
	ImGui::Separator();

	if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {

		SubmitPendingSceneRequest(true);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Don't Save", ImVec2(120.0f, 0.0f))) {

		SubmitPendingSceneRequest(false);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {

		pendingSceneRequest_ = {};
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::EditorManager::ExecuteSceneMeshPicking(GraphicsCore& graphicsCore,
	[[maybe_unused]] const EditorContext& context, const RenderPipelineRunner& renderPipeline) {

	// 以下の条件のいずれかを満たす場合はピック処理を行わない
	if (!initialized_ || !layoutState_.showSceneView ||
		editorState_.useSceneGizmo || !editorState_.enableScenePick) {
		return;
	}

	Input* input = Input::GetInstance();
	// シーンビューの上で左クリックされたフレームのみ処理する
	if (!input->HasViewRect(InputViewArea::Scene)) {
		return;
	}
	if (!input->IsMouseOnView(InputViewArea::Scene)) {
		return;
	}
	if (!input->TriggerMouse(MouseButton::Left)) {
		return;
	}

	// マウス座標を取得
	const std::optional<Vector2> mousePosInView = input->GetMousePosInView(InputViewArea::Scene);
	if (!mousePosInView.has_value()) {
		return;
	}

	// メッシュピック処理を実行
	meshSubMeshPicker_->ExecutePick(graphicsCore, renderPipeline.GetResolvedView(RenderViewKind::Scene),
		mousePosInView.value(), renderPipeline.GetSceneViewPickRecords(), renderPipeline.GetSceneViewTLASResource());
}

void Engine::EditorManager::HandleGlobalShortcuts(const EditorContext& context) {

	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput) {
		return;
	}

	// 処理を戻す
	if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) {

		UndoEditorCommand();
		return;
	}
	// 処理を進める
	if ((io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) ||
		(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))) {

		RedoEditorCommand();
		return;
	}
	// 複製
	if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D)) {

		DuplicateSelection();
		return;
	}
	// シーン保存
	if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {

		RequestSaveScene();
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
	// 削除
	if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
		if (editorState_.HasValidSelection(context.activeWorld) && !context.isPlaying) {

			ExecuteEditorCommand(std::make_unique<DeleteEntityCommand>(editorState_.selectedEntity));
		}
	}
}

void Engine::EditorManager::BeginFrame(GraphicsCore& graphicsCore, const EditorContext& context) {

	if (!initialized_) {
		return;
	}

	// 現在のレンダリングコンテキストを保存
	currentRenderContext_ = &context;

	// シーンビューのメッシュピック処理の結果を使用する
	meshSubMeshPicker_->ConsumePendingResult(context.activeWorld, editorState_);

	editorState_.ValidateSelection(context.activeWorld);

	// フレーム開始
	imguiManager_.Begin();
	ImGuizmo::BeginFrame();
	DrawDockSpace();

	// シーンビューのマニュアルカメラを更新
	UpdateSceneViewManualCamera();

	// 各パネルの描画
	EditorPanelContext panelContext{};
	panelContext.editorContext = &context;
	panelContext.editorState = &editorState_;
	panelContext.layoutState = &layoutState_;
	panelContext.host = this;
	panelContext.viewportRenderService = nullptr;
	panelContext.graphicsPlatform = &graphicsCore.GetDXObject();

	// グローバルショートカットの処理
	HandleGlobalShortcuts(context);
	DrawPanelsByPhase(panelContext, EditorPanelPhase::PreScene);
	DrawUnsavedScenePopup();
}

void Engine::EditorManager::EndFrame(GraphicsCore& graphicsCore, const EditorContext& context,
	const ViewportRenderService* viewportRenderService, const ResolvedRenderView* sceneRenderView) {

	if (!initialized_) {
		return;
	}

	// 各パネルの描画
	EditorPanelContext panelContext{};
	panelContext.editorContext = &context;
	panelContext.editorState = &editorState_;
	panelContext.layoutState = &layoutState_;
	panelContext.host = this;
	panelContext.viewportRenderService = viewportRenderService;
	panelContext.graphicsPlatform = &graphicsCore.GetDXObject();
	panelContext.sceneRenderView = sceneRenderView;

	// ドッキングスペースの描画
	DrawPanelsByPhase(panelContext, EditorPanelPhase::PostScene);

	imguiManager_.End();

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	const auto& window = graphicsCore.GetContext().GetWindowSetting();

	dxCommand->BindRenderTargets(std::optional<RenderTarget>(graphicsCore.GetBackBufferRenderTarget()),
		graphicsCore.GetDSVDescriptor().GetFrameCPUHandle());

	dxCommand->SetViewportAndScissor(window.engineSize.x, window.engineSize.y);
	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	imguiManager_.Draw(dxCommand->GetCommandList());

	currentRenderContext_ = nullptr;
}

void Engine::EditorManager::DrawPanelsByPhase(const EditorPanelContext& context, EditorPanelPhase phase) {

	for (const auto& panel : panels_) {
		if (panel->GetPhase() != phase) {
			continue;
		}
		panel->Draw(context);
	}
}

void Engine::EditorManager::UpdateSceneViewManualCamera() {

	// ビューが非表示なら更新しない
	// カメラモードがマニュアルでないなら更新しない
	// シーンギズモを使用している場合は更新しない
	if (!layoutState_.showSceneView ||
		editorState_.sceneViewCamera.mode != SceneViewCameraMode::DebugManual ||
		editorState_.useSceneGizmo) {
		return;
	}
	// カメラの状態を更新する
	SceneViewCameraController::Update(sceneViewCameraState_, editorState_.manualCameraDimension);
}

void Engine::EditorManager::Finalize() {

	if (!initialized_) {
		return;
	}

	// レイアウトを保存
	ImGui::SaveIniSettingsToDisk(kEditorLayoutIniPath);

	imguiManager_.Finalize();
	initialized_ = false;
	requestTogglePlay_ = false;

	for (uint32_t i = 0; i < panels_.size(); ++i) {
		panels_[i].reset();
	}

	meshSubMeshPicker_->Finalize();
	meshSubMeshPicker_.reset();
}

bool Engine::EditorManager::ConsumePlayToggleRequest() {

	const bool requested = requestTogglePlay_;
	requestTogglePlay_ = false;
	return requested;
}

Engine::EditorSceneRequest Engine::EditorManager::ConsumeSceneRequest() {

	EditorSceneRequest request = sceneRequest_;
	sceneRequest_ = {};
	return request;
}

void Engine::EditorManager::MarkActiveSceneSaved() {

	activeSceneDirty_ = false;
}

void Engine::EditorManager::ResetSceneEditingState() {

	editorState_.ClearSelection();
	editorState_.commandHistory.Clear();
	pendingSceneRequest_ = {};
	requestOpenUnsavedPopup_ = false;
	activeSceneDirty_ = false;
}

void Engine::EditorManager::DrawDockSpace() {

	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	// ドッキングスペースのホストウィンドウのフラグ
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
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

	ImGui::DockSpace(ImGui::GetID(kDockSpaceID), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	ImGui::End();
}
