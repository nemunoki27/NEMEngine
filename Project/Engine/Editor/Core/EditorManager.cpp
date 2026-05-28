#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Core/D3D12CommandContext.h>
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
#include <Engine/Core/Platform/Input/InputSystem.h>

// パネル群
#include <Engine/Editor/UI/Panels/Builtin/MenuBarPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ToolbarPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/HierarchyPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/InspectorPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ProjectPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ConsolePanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ViewportPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/SceneViewToolPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ToolPanel.h>
#include <Engine/Editor/Tools/Builtin/BuiltinEditorTools.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <algorithm>

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
	// ImGuiのレイアウト保存ファイルパス
	constexpr const char* kEditorLayoutIniPath = "EditorLayout.ini";

	bool IsHidePanelsShortcutTriggered() {

		Engine::Input* input = Engine::Input::GetInstance();
		const bool directInputDown = input &&
			input->PushKey(DIK_TAB) && input->PushKey(DIK_ESCAPE);

		const bool imguiDown =
			ImGui::IsKeyDown(ImGuiKey_Tab) && ImGui::IsKeyDown(ImGuiKey_Escape);

		const bool shortcutDown = directInputDown || imguiDown;

		// 同時押しに入った瞬間だけ反応させる。押しっぱなしの間は再トグルしない。
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
	requestOpenCloseUnsavedPopup_ = false;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
	activeSceneDirty_ = false;

	// エディタ標準ツールの登録
	RegisterBuiltinEditorTools();
	// シーンビューカメラツールを取得
	sceneViewCameraController_ = static_cast<SceneViewCameraController*>(
		Engine::ToolRegistry::GetInstance().Find("engine.sceneViewCamera"));

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

	ImGui::TextUnformatted("現在のシーンは変更後、保存されていません");
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

	ImGui::TextUnformatted("現在のシーンは変更後、保存されていません");
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

Engine::Entity Engine::EditorManager::Execute2DPick(const Vector2& inputPixel, const ResolvedRenderView& view, ECSWorld* world) {

	if (!world) {
		return Entity::Null();
	}

	const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Orthographic);
	if (!camera) {
		return Entity::Null();
	}

	// NDC座標への変換
	// inputPixelはGetMousePosInView()により、描画元(view.width, view.height)の解像度にスケーリングされた座標
	float ndcX = (inputPixel.x / static_cast<float>(view.width)) * 2.0f - 1.0f;
	float ndcY = 1.0f - (inputPixel.y / static_cast<float>(view.height)) * 2.0f;

	Vector3 ndcOrigin(ndcX, ndcY, 0.0f);
	Vector3 ndcTarget(ndcX, ndcY, 1.0f);

	struct HitRecord {
		Entity entity;
		int32_t layer;
		int32_t order;
	};
	std::vector<HitRecord> hits;

	world->ForEach<SpriteRendererComponent>([&](const Entity& entity, const SpriteRendererComponent& renderer) {
		if (!RenderItemExtract::IsVisible(*world, entity, renderer.visible)) {
			return;
		}

		Matrix4x4 worldMatrix = RenderItemExtract::GetWorldMatrix(*world, entity);
		Matrix4x4 wvp = worldMatrix * camera->matrices.viewProjectionMatrix;
		Matrix4x4 wvpInv = Matrix4x4::Inverse(wvp);

		// NDCからローカル空間へのレイを計算
		Vector3 localOrigin = Vector3::Transform(ndcOrigin, wvpInv);
		Vector3 localTarget = Vector3::Transform(ndcTarget, wvpInv);
		Vector3 localDir = Vector3::Normalize(localTarget - localOrigin);

		// Z=0平面との交差判定 (rd.zが0に近い場合は平行なのでスキップ)
		if (std::abs(localDir.z) < 1e-5f) {
			return;
		}

		float t = -localOrigin.z / localDir.z;
		// 後ろにあるものはピッキングしない
		if (t < 0.0f) {
			return;
		}

		Vector3 hitPoint = localOrigin + localDir * t;

		// スプライトの矩形領域内か判定
		float minX = -renderer.pivot.x * renderer.size.x;
		float maxX = (1.0f - renderer.pivot.x) * renderer.size.x;
		float minY = -renderer.pivot.y * renderer.size.y;
		float maxY = (1.0f - renderer.pivot.y) * renderer.size.y;

		if (hitPoint.x >= minX && hitPoint.x <= maxX &&
			hitPoint.y >= minY && hitPoint.y <= maxY) {
			hits.push_back({ entity, renderer.layer, renderer.order });
		}
	});

	world->ForEach<TextRendererComponent>([&](const Entity& entity, const TextRendererComponent& renderer) {
		if (!RenderItemExtract::IsVisible(*world, entity, renderer.visible)) {
			return;
		}
		if (!renderer.runtimeLayout.valid || renderer.runtimeLayout.glyphs.empty()) {
			return;
		}

		Matrix4x4 worldMatrix = RenderItemExtract::GetWorldMatrix(*world, entity);
		Matrix4x4 wvp = worldMatrix * camera->matrices.viewProjectionMatrix;
		Matrix4x4 wvpInv = Matrix4x4::Inverse(wvp);

		// NDCからローカル空間へのレイを計算
		Vector3 localOrigin = Vector3::Transform(ndcOrigin, wvpInv);
		Vector3 localTarget = Vector3::Transform(ndcTarget, wvpInv);
		Vector3 localDir = Vector3::Normalize(localTarget - localOrigin);

		if (std::abs(localDir.z) < 1e-5f) {
			return;
		}

		float t = -localOrigin.z / localDir.z;
		if (t < 0.0f) {
			return;
		}

		Vector3 hitPoint = localOrigin + localDir * t;

		// テキストの全体の矩形を計算
		float minX = (std::numeric_limits<float>::max)();
		float maxX = -(std::numeric_limits<float>::max)();
		float minY = (std::numeric_limits<float>::max)();
		float maxY = -(std::numeric_limits<float>::max)();

		for (const auto& glyph : renderer.runtimeLayout.glyphs) {
			minX = (std::min)(minX, glyph.rectMin.x);
			maxX = (std::max)(maxX, glyph.rectMax.x);
			minY = (std::min)(minY, glyph.rectMin.y);
			maxY = (std::max)(maxY, glyph.rectMax.y);
		}

		if (hitPoint.x >= minX && hitPoint.x <= maxX &&
			hitPoint.y >= minY && hitPoint.y <= maxY) {
			hits.push_back({ entity, renderer.layer, renderer.order });
		}
	});

	if (hits.empty()) {
		return Entity::Null();
	}

	// レイヤー、オーダーの降順でソート（手前にあるものを優先）
	std::sort(hits.begin(), hits.end(), [](const HitRecord& a, const HitRecord& b) {
		if (a.layer != b.layer) return a.layer > b.layer;
		return a.order > b.order;
	});

	return hits.front().entity;
}

void Engine::EditorManager::ExecuteSceneMeshPicking(GraphicsCore& graphicsCore,
	[[maybe_unused]] const EditorContext& context, const RenderPipelineRunner& renderPipeline) {

	// 以下の条件のいずれかを満たす場合はピック処理を行わない
	if (!initialized_ || layoutState_.hidePanels || !editorState_.enableScenePick) {
		return;
	}

	Input* input = Input::GetInstance();
	auto executePick = [&](InputViewArea inputArea, RenderViewKind viewKind,
		ID3D12Resource* tlasResource, const std::vector<MeshSubMeshPickRecord>& pickRecords) {

			// ビューの上で左クリックされたフレームのみ処理する
			if (!input->HasViewRect(inputArea)) {
				return false;
			}
			if (!input->IsMouseOnView(inputArea)) {
				return false;
			}
			if (!input->TriggerMouse(MouseButton::Left)) {
				return false;
			}

			// マウス座標を取得
			const std::optional<Vector2> mousePosInView = input->GetMousePosInView(inputArea);
			if (!mousePosInView.has_value()) {
				return false;
			}

			// 2Dエンティティのピック処理を優先実行
			Entity hitEntity2D = Execute2DPick(mousePosInView.value(), renderPipeline.GetResolvedView(viewKind), context.activeWorld);
			if (hitEntity2D.IsValid()) {
				
				// 2Dが優先されるため、GPUによる3Dピックは行わず、即座に選択を確定する
				editorState_.SelectFromScenePick(hitEntity2D, 0);
				return true;
			}

			// メッシュピック処理を実行
			meshSubMeshPicker_->ExecutePick(graphicsCore, renderPipeline.GetResolvedView(viewKind),
				mousePosInView.value(), pickRecords, tlasResource);
			return true;
		};

	// SceneViewは従来通り、ギズモ操作中はピックしない
	if (layoutState_.showSceneView && !editorState_.useSceneGizmo) {
		if (executePick(InputViewArea::Scene, RenderViewKind::Scene,
			renderPipeline.GetSceneViewTLASResource(), renderPipeline.GetSceneViewPickRecords())) {
			return;
		}
	}

	// GameViewにもSceneViewと同じTLASピックだけを通し、マニピュレーターは表示しない。
	if (layoutState_.showGameView) {
		executePick(InputViewArea::Game, RenderViewKind::Game,
			renderPipeline.GetGameViewTLASResource(), renderPipeline.GetGameViewPickRecords());
	}
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

	// シーンビューのメッシュピック処理の結果を使用する
	meshSubMeshPicker_->ConsumePendingResult(context.activeWorld, editorState_);

	editorState_.ValidateSelection(context.activeWorld);

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
	panelContext.graphicsCore = &graphicsCore;
	panelContext.graphicsPlatform = &graphicsCore.GetDXObject();

	// グローバルショートカットの処理
	HandleGlobalShortcuts(context);
	DrawPanelsByPhase(panelContext, EditorPanelPhase::PreScene);
	DrawUnsavedScenePopup();
	DrawCloseUnsavedScenePopup();
}

void Engine::EditorManager::DrawSceneDebugObjects(const EditorContext& context) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (!initialized_ || layoutState_.hidePanels || !layoutState_.showSceneView || !context.activeWorld) {
		return;
	}
	if (!editorState_.HasValidSelection(context.activeWorld)) {
		return;
	}

	InspectorDrawerCommon::DrawEntityDebugObject(*context.activeWorld, editorState_.selectedEntity);
#else
	(void)context;
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

	// ドッキングスペースの描画
	DrawPanelsByPhase(panelContext, EditorPanelPhase::PostScene);

	//ImGui::ShowDemoWindow();

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

	if (context.layoutState && context.layoutState->hidePanels) {
		return;
	}

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

	// ツールウィンドウ等がシーンビューの上に重なっている場合はカメラを更新しない
	// HoveredWindowが前フレームのマウス下ウィンドウを示すため、SceneView以外はスキップする
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	if (ctx && ctx->HoveredWindow) {
		const char* name = ctx->HoveredWindow->Name;
		if (name && std::strstr(name, "SceneView") == nullptr) {
			return;
		}
	}

	// カメラの状態を更新する
	sceneViewCameraController_->Update(editorState_.manualCameraDimension, InputViewArea::Scene);
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
	requestOpenCloseUnsavedPopup_ = false;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
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
