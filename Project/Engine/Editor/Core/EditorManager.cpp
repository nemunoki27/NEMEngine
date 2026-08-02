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
#include <Engine/Editor/Commands/Entity/EditorEntityDuplicateUtility.h>
#include <Engine/Editor/Commands/Entity/CreateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Editor/Commands/Entity/DuplicateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/PasteEntityTreeCommand.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/Core/SceneViewInteractionPolicy.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// パネル群
#include <Engine/Editor/UI/Panels/Builtin/BuiltinEditorPanelRegistration.h>
#include <Engine/Editor/Tools/Builtin/BuiltinEditorTools.h>
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
	constexpr const char* kUnsavedScenePopupName = "シーン未保存通知";
	constexpr const char* kCloseUnsavedScenePopupName = "シーン未保存通知##CloseApplication";
	bool IsHidePanelsShortcutTriggered() {

		Engine::Input* input = Engine::Input::GetInstance();
		const bool directInputDown = input &&
			input->PushKey(DIK_TAB) && input->PushKey(DIK_ESCAPE);

		const bool imguiDown =
			ImGui::IsKeyDown(ImGuiKey_Tab) && ImGui::IsKeyDown(ImGuiKey_Escape);

		const bool shortcutDown = directInputDown || imguiDown;

		// 同時押しに入った瞬間だけ反応させ押しっぱなしの間は再トグルしない
		static bool wasShortcutDown = false;
		const bool triggered = shortcutDown && !wasShortcutDown;
		wasShortcutDown = shortcutDown;
		return triggered;
	}

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
		graphicsPlatform.GetDevice(), graphicsPlatform.GetCommandQueue()->GetQueue(),
		&graphicsCore.GetSRVDescriptor(), graphicsSetting.swapChainFormat, DXGI_FORMAT_D24_UNORM_S8_UINT);

	// ImGuizmoのImGuiコンテキストを設定
	ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

	// ImGuiのレイアウトはEditorLayoutManagerで管理する
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;

	// レイアウト構築フラグをリセット
	initialized_ = true;
	requestTogglePlay_ = false;
	requestResumePlay_ = false;
	requestPausePlay_ = false;
	requestPlayFrameStep_ = false;
	sceneRequest_ = {};
	pendingSceneRequest_ = {};
	requestOpenUnsavedPopup_ = false;
	requestOpenCloseUnsavedPopup_ = false;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
	dirtySceneAssets_.clear();
	dirtySceneRevisions_.clear();
	dirtySceneRevision_ = 0;
	pendingDuplicatePanelID_.clear();
	pendingEditorLayout_.reset();
	requestBuildDefaultDockLayout_ = false;

	// エディタ標準ツールの登録
	RegisterBuiltinEditorTools();
	// シーンビューカメラツールを取得
	sceneViewCameraController_ = static_cast<SceneViewCameraController*>(
		Engine::ToolRegistry::GetInstance().Find("engine.sceneViewCamera"));
	LoadViewportPanelState();

	// 各パネルの生成と登録
	EditorPanelCreateContext panelCreateContext{ graphicsCore.GetTextureUploadService() };
	for (auto& panel : CreateBuiltinEditorPanels(panelCreateContext)) {
		panels_.emplace_back(std::move(panel));
	}

	// 保存済みセッションが無ければエンジンのDefaultレイアウトを適用する
	editorLayoutManager_.Init();
	EditorLayoutSnapshot startupLayout{};
	if (editorLayoutManager_.LoadStartupLayout(startupLayout)) {
		ApplyEditorLayout(startupLayout, graphicsCore);
	}

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
		MarkCurrentSceneDirty();
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
		MarkCurrentSceneDirty();
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
		MarkCurrentSceneDirty();
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
	// 複数選択を順に複製する、選択や生存が変わるため対象を先にコピーしておく
	ECSWorld* world = currentRenderContext_->activeWorld;
	const std::vector<Entity> targets = editorState_.GetSelectedEntities();
	std::vector<Entity> duplicated;
	for (const Entity& target : targets) {
		if (world && world->IsAlive(target)) {
			// 各コマンドは複製ルートをselectedEntityへ入れるので実行後に集約する
			if (ExecuteEditorCommand(std::make_unique<DuplicateEntityCommand>(target))) {
				duplicated.push_back(editorState_.selectedEntity);
			}
		}
	}
	if (duplicated.empty()) {
		return false;
	}
	// 複製した分をまとめて選択し直す
	editorState_.SetSelectedEntities(duplicated);
	return true;
}

bool Engine::EditorManager::CopySelectionToClipboardInternal(const EditorContext& context) {

	if (!editorState_.HasValidSelection(context.activeWorld) || context.isPlaying) {
		return false;
	}

	ECSWorld& world = *context.activeWorld;

	// 複数選択をそれぞれ独立スナップショットとしてクリップボードへ保存する
	editorState_.clipboardSnapshots.clear();
	editorState_.clipboardParentUUIDs.clear();
	const std::vector<Entity> targets = editorState_.GetSelectedEntities();
	for (const Entity& selected : targets) {

		if (!world.IsAlive(selected)) {
			continue;
		}
		EditorEntityTreeSnapshot snapshot{};
		EditorEntitySnapshotUtility::CaptureSubtree(world, selected, snapshot);
		if (snapshot.IsEmpty()) {
			continue;
		}

		// 各エンティティの親UUIDも控えておき、貼り付けは元の親付近へ行う
		UUID parentUUID{};
		if (world.HasComponent<HierarchyComponent>(selected)) {

			const auto& hierarchy = world.GetComponent<HierarchyComponent>(selected);
			if (world.IsAlive(hierarchy.parent)) {
				parentUUID = world.GetUUID(hierarchy.parent);
			}
		}
		// クリップボードは外部親を持たない独立スナップショットにしておく
		EditorEntityDuplicateUtility::ClearRootParentLink(snapshot);
		editorState_.clipboardSnapshots.emplace_back(std::move(snapshot));
		editorState_.clipboardParentUUIDs.emplace_back(parentUUID);
	}
	return !editorState_.clipboardSnapshots.empty();
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
	// クリップボードの各スナップショットを順に貼り付け、貼り付け先をまとめて選択する
	std::vector<Entity> pasted;
	for (size_t i = 0; i < editorState_.clipboardSnapshots.size(); ++i) {

		const UUID parentUUID = i < editorState_.clipboardParentUUIDs.size() ?
			editorState_.clipboardParentUUIDs[i] : UUID{};
		if (ExecuteEditorCommand(std::make_unique<PasteEntityTreeCommand>(
			editorState_.clipboardSnapshots[i], parentUUID))) {
			pasted.push_back(editorState_.selectedEntity);
		}
	}
	if (pasted.empty()) {
		return false;
	}
	editorState_.SetSelectedEntities(pasted);
	return true;
}

void Engine::EditorManager::RequestPlayToggle() {

	// プレイ要求フラグを立てる
	requestTogglePlay_ = true;
}

void Engine::EditorManager::RequestPlayResume() {

	requestResumePlay_ = true;
}

void Engine::EditorManager::RequestPlayPause() {

	requestPausePlay_ = true;
}

void Engine::EditorManager::RequestPlayFrameStep() {

	requestPlayFrameStep_ = true;
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

void Engine::EditorManager::RequestMarkSceneDirty() {

	MarkCurrentSceneDirty();
}

void Engine::EditorManager::RequestEnterPrefabEdit(AssetID prefabAsset) {

	if (!prefabAsset) {
		return;
	}
	sceneRequest_ = { EditorSceneRequestType::EnterPrefabEdit, prefabAsset };
}

void Engine::EditorManager::RequestExitPrefabEdit() {

	sceneRequest_ = { EditorSceneRequestType::ExitPrefabEdit, AssetID{} };
}

void Engine::EditorManager::RequestExitPrefabEditAll() {

	sceneRequest_ = { EditorSceneRequestType::ExitPrefabEditAll, AssetID{} };
}

void Engine::EditorManager::RequestTogglePrefabInContext() {

	sceneRequest_ = { EditorSceneRequestType::TogglePrefabInContext, AssetID{} };
}

void Engine::EditorManager::RequestSavePrefab() {

	sceneRequest_ = { EditorSceneRequestType::SavePrefab, AssetID{} };
}

void Engine::EditorManager::RequestCloseUnsavedScenePopup() {

	requestOpenCloseUnsavedPopup_ = true;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
}

Engine::EditorUnsavedScenePopupResult Engine::EditorManager::ConsumeCloseUnsavedScenePopupResult() {

	EditorUnsavedScenePopupResult result = closeUnsavedScenePopupResult_;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
	return result;
}

void Engine::EditorManager::QueueSceneRequest(const EditorSceneRequest& request) {

	if (request.type == EditorSceneRequestType::NewScene ||
		request.type == EditorSceneRequestType::OpenScene) {

		if (HasDirtyScenes()) {

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
		return "新しいシーンを作成する";
	case EditorSceneRequestType::OpenScene:
		return "別のシーンを開く";
	default:
		return "シーンを切り替える";
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

	ImGui::TextUnformatted("読み込み中のシーンに未保存の変更があります");
	ImGui::Text("%s前に保存しますか？", GetSceneRequestActionName(pendingSceneRequest_.type));
	ImGui::Separator();

	if (ImGui::Button("保存", ImVec2(120.0f, 0.0f))) {

		SubmitPendingSceneRequest(true);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("保存しない", ImVec2(120.0f, 0.0f))) {

		SubmitPendingSceneRequest(false);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {

		pendingSceneRequest_ = {};
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::EditorManager::DrawCloseUnsavedScenePopup() {

	if (requestOpenCloseUnsavedPopup_) {

		ImGui::OpenPopup(kCloseUnsavedScenePopupName);
		requestOpenCloseUnsavedPopup_ = false;
	}

	if (!ImGui::BeginPopupModal(kCloseUnsavedScenePopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextUnformatted("読み込み中のシーンに未保存の変更があります");
	ImGui::TextUnformatted("保存しますか？");
	ImGui::Separator();

	if (ImGui::Button("保存", ImVec2(120.0f, 0.0f))) {

		closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::Save;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("保存しない", ImVec2(120.0f, 0.0f))) {

		closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::DontSave;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {

		closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::Cancel;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::EditorManager::HandleGlobalShortcuts(const EditorContext& context) {

	if (editorCommandPanelKind_ !=
		EditorCommandPanelKind::Scene) {
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
	// シーン保存、プレファブ編集中は隔離ワールドを.prefabへ保存する
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
	// 削除、複数選択をまとめて消すため対象を先にコピーしてからループする
	if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
		if (editorState_.HasValidSelection(context.activeWorld) && !context.isPlaying) {

			const std::vector<Entity> targets = editorState_.GetSelectedEntities();
			for (const Entity& target : targets) {
				if (context.activeWorld && context.activeWorld->IsAlive(target)) {
					ExecuteEditorCommand(std::make_unique<DeleteEntityCommand>(target));
				}
			}
		}
		return;
	}
	// ギズモ操作のショートカット
	{
		// シーンビューにカーソルが合っているときのみ

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

	// 現在のレンダリングコンテキストを保存
	currentRenderContext_ = &context;

	// 前フレームのパネル操作をImGuiフレーム開始前に反映する
	RemoveClosedDuplicatedPanels();
	ApplyPendingEditorLayout(graphicsCore);

	// フレーム開始
	imguiManager_.Begin();
	if (!layoutState_.hidePanels && IsHidePanelsShortcutTriggered()) {

		// 通常表示中でもMenuBarのショートカット表記通りTab+EscでHidePanelsへ入る
		layoutState_.hidePanels = true;
		return;
	}
	if (layoutState_.hidePanels) {

		// HidePanels中はエディター機能を止め、Tab+Escの復帰入力だけを受け付ける
		if (IsHidePanelsShortcutTriggered()) {
			layoutState_.hidePanels = false;
		} else {
			return;
		}
	}

	// シーンビューのメッシュピック処理の結果を選択状態へ適用する
	const MeshSubMeshPickOutcome pickOutcome =
		meshSubMeshPicker_->ConsumePendingResult(
			graphicsCore, context.activeWorld);
	if (pickOutcome.committed) {

		// ヒットなしは選択解除、ヒット時のみ候補サブメッシュを更新する
		editorState_.scenePickDragEntity = pickOutcome.hit ? pickOutcome.entity : Entity::Null();
		if (pickOutcome.hit) {
			editorState_.scenePickCandidateSubMesh = pickOutcome.subMeshIndex;
			editorState_.scenePickCandidateSubMeshID = pickOutcome.subMeshStableID;
		}
		editorState_.CommitScenePick(*context.activeWorld);
	}

	editorState_.ValidateSelection(context.activeWorld);

	ImGuizmo::BeginFrame();
	DrawDockSpace();

	// ダブルクリックで要求されたフォーカスを消費する、3DマニュアルカメラのときだけEntityへ寄せる
	if (sceneViewCameraController_ && editorState_.cameraFocusRequest.IsValid()) {

		ECSWorld* focusWorld = context.activeWorld;
		const Entity focusTarget = editorState_.cameraFocusRequest;
		editorState_.cameraFocusRequest = Entity::Null();
		if (focusWorld && focusWorld->IsAlive(focusTarget) &&
			editorState_.manualCameraDimension == Dimension::Type3D) {

			sceneViewCameraController_->FocusOn(
				RenderItemExtract::GetWorldMatrix(*focusWorld, focusTarget).GetTranslationValue());
		}
	}
	// フォーカス中の寄りを毎フレーム進める、入力可否に関わらず行う
	if (sceneViewCameraController_) {

		sceneViewCameraController_->UpdateFocus();
		// フォーカス中はギズモ操作を無効にして誤移動を防ぐ
		editorState_.cameraFocusing = sceneViewCameraController_->IsFocusing();
	}

	// シーンビューのマニュアルカメラを更新
	UpdateSceneViewManualCamera();

	// 各パネルの描画
	editorCommandPanelKind_ = EditorCommandPanelKind::None;
	EditorPanelContext panelContext{};
	panelContext.editorContext = &context;
	panelContext.editorState = &editorState_;
	panelContext.layoutState = &layoutState_;
	panelContext.host = this;
	panelContext.viewportRenderService = nullptr;
	panelContext.graphicsCore = &graphicsCore;
	panelContext.graphicsPlatform = &graphicsCore.GetDXObject();

	DrawPanelsByPhase(panelContext, EditorPanelPhase::PreScene);
	DrawUnsavedScenePopup();
	DrawCloseUnsavedScenePopup();
}

void Engine::EditorManager::DrawSceneDebugObjects([[maybe_unused]] const EditorContext& context) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (!initialized_ || layoutState_.hidePanels || !layoutState_.showSceneView || !context.activeWorld) {
		return;
	}

	// スナップグリッドの表示判定の結果を描画するだけにする
	const SceneViewSnapGridDecision gridDecision =
		ResolveSceneViewSnapGridDecision(editorState_, context.activeWorld);
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

	// サブメッシュ単位選択中はそのサブメッシュ番号を、エンティティ選択中は-1を渡す
	int32_t selectionSubMeshIndex = -1;
	uint32_t resolvedSubMeshIndex = 0;
	if (editorState_.HasValidSubMeshSelection(context.activeWorld) &&
		editorState_.TryResolveSelectedSubMeshIndex(context.activeWorld, resolvedSubMeshIndex)) {
		selectionSubMeshIndex = static_cast<int32_t>(resolvedSubMeshIndex);
	}
	// 複数選択時は全選択にアウトラインを出す、サブメッシュ番号はアクティブのみ反映し他は全体
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

		// ImGuiフレームは入力更新のために開始しているが、描画コマンドは発行しない
		imguiManager_.End();
		currentRenderContext_ = nullptr;
		return;
	}

	// 各パネルの描画
	EditorPanelContext panelContext{};
	panelContext.editorContext = &context;
	panelContext.editorState = &editorState_;
	panelContext.layoutState = &layoutState_;
	panelContext.host = this;
	panelContext.viewportRenderService = viewportRenderService;
	panelContext.graphicsCore = &graphicsCore;
	panelContext.graphicsPlatform = &graphicsCore.GetDXObject();
	panelContext.renderPipeline = renderPipeline;
	panelContext.sceneRenderView = sceneRenderView;
	panelContext.sceneViewCamera = GetSceneViewCameraState();

	// ドッキングスペースの描画
	DrawPanelsByPhase(panelContext, EditorPanelPhase::PostScene);
	// 全パネルのフォーカスが確定してからメイン編集コマンドを処理
	HandleGlobalShortcuts(context);
	ApplyPendingPanelDuplicate(panelContext);

	//ImGui::ShowDemoWindow();

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
	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	imguiManager_.Draw(dxCommand->GetCommandList());

	currentRenderContext_ = nullptr;
}

void Engine::EditorManager::RenderPlatformWindows() {

	if (!initialized_) {
		return;
	}
	imguiManager_.DrawPlatformWindows();
}

void Engine::EditorManager::Finalize() {

	if (!initialized_) {
		return;
	}

	// 現在のパネル構成とドック状態をユーザーセッションへ保存する
	editorLayoutManager_.SaveSession(CaptureEditorLayout());
	SaveViewportPanelState();

	imguiManager_.Finalize();
	initialized_ = false;
	requestTogglePlay_ = false;
	requestResumePlay_ = false;
	requestPausePlay_ = false;
	requestPlayFrameStep_ = false;
	pendingDuplicatePanelID_.clear();
	pendingEditorLayout_.reset();
	requestBuildDefaultDockLayout_ = false;

	panels_.clear();

	meshSubMeshPicker_->Finalize();
	meshSubMeshPicker_.reset();
}

bool Engine::EditorManager::ConsumePlayToggleRequest() {

	const bool requested = requestTogglePlay_;
	requestTogglePlay_ = false;
	return requested;
}

bool Engine::EditorManager::ConsumePlayResumeRequest() {

	const bool requested = requestResumePlay_;
	requestResumePlay_ = false;
	return requested;
}

bool Engine::EditorManager::ConsumePlayPauseRequest() {

	const bool requested = requestPausePlay_;
	requestPausePlay_ = false;
	return requested;
}

bool Engine::EditorManager::ConsumePlayFrameStepRequest() {

	const bool requested = requestPlayFrameStep_;
	requestPlayFrameStep_ = false;
	return requested;
}

Engine::EditorSceneRequest Engine::EditorManager::ConsumeSceneRequest() {

	EditorSceneRequest request = sceneRequest_;
	sceneRequest_ = {};
	return request;
}

void Engine::EditorManager::MarkSceneSaved(AssetID sceneAsset) {

	dirtySceneAssets_.erase(sceneAsset);
	dirtySceneRevisions_.erase(sceneAsset);
}

void Engine::EditorManager::MarkSceneSaved(
	AssetID sceneAsset, uint64_t dirtyRevision) {

	if (GetSceneDirtyRevision(sceneAsset) != dirtyRevision) {
		return;
	}
	MarkSceneSaved(sceneAsset);
}

void Engine::EditorManager::MarkAllScenesSaved() {

	dirtySceneAssets_.clear();
	dirtySceneRevisions_.clear();
}

void Engine::EditorManager::ResetSceneEditingState() {

	editorState_.ClearSelection();
	editorState_.commandHistory.Clear();
	pendingSceneRequest_ = {};
	requestOpenUnsavedPopup_ = false;
	requestOpenCloseUnsavedPopup_ = false;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
}

void Engine::EditorManager::ResetSceneDirtyState() {

	dirtySceneAssets_.clear();
	dirtySceneRevisions_.clear();
}

bool Engine::EditorManager::IsSceneDirty(AssetID sceneAsset) const {

	return sceneAsset && dirtySceneAssets_.contains(sceneAsset);
}

uint64_t Engine::EditorManager::GetSceneDirtyRevision(
	AssetID sceneAsset) const {

	const auto it = dirtySceneRevisions_.find(sceneAsset);
	return it != dirtySceneRevisions_.end() ?
		it->second : 0;
}

void Engine::EditorManager::MarkCurrentSceneDirty() {

	if (!currentRenderContext_ || currentRenderContext_->isPlaying ||
		currentRenderContext_->isPrefabEditing || !currentRenderContext_->activeSceneAsset) {
		return;
	}
	dirtySceneAssets_.insert(currentRenderContext_->activeSceneAsset);
	dirtySceneRevisions_[
		currentRenderContext_->activeSceneAsset] =
		++dirtySceneRevision_;
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
	ImGui::DockBuilderDockWindow("Tool", consoleDockID);
	ImGui::DockBuilderDockWindow("SceneView", mainDockID);
	ImGui::DockBuilderDockWindow("GameView", mainDockID);
	ImGui::DockBuilderFinish(dockSpaceID);
}
