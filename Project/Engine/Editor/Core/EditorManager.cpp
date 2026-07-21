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
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <algorithm>
#include <filesystem>
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
	constexpr const char* kViewportPanelStateConfigPath = Engine::ConfigPaths::kViewportPanel;

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

	template <typename Enum>
	void LoadEnumValue(const nlohmann::json& data, const char* key, Enum& value) {

		if (!data.contains(key) || !data[key].is_string()) {
			return;
		}

		if (std::optional<Enum> loaded = Engine::EnumAdapter<Enum>::FromString(data[key].get<std::string>())) {
			value = loaded.value();
		}
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
	activeSceneDirty_ = false;
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

void Engine::EditorManager::RequestDuplicatePanel(const std::string& instanceID) {

	pendingDuplicatePanelID_ = instanceID;
}

const std::vector<Engine::EditorLayoutMenuEntry>& Engine::EditorManager::GetEditorLayoutEntries() const {

	return editorLayoutManager_.GetMenuEntries();
}

const std::string& Engine::EditorManager::GetActiveEditorLayoutID() const {

	return editorLayoutManager_.GetActiveLayoutID();
}

bool Engine::EditorManager::IsEngineLayoutSaveAvailable() const {

	return editorLayoutManager_.IsEngineSourceProject();
}

bool Engine::EditorManager::RequestSaveEditorLayout(const std::string& name, std::string& outError) {

	EditorLayoutSnapshot layout = CaptureEditorLayout();
	std::string layoutID;
	if (!editorLayoutManager_.SaveUserLayout(name, layout, layoutID, outError)) {
		return false;
	}

	if (const EditorLayoutSnapshot* savedLayout = editorLayoutManager_.FindLayout(layoutID)) {
		editorLayoutManager_.SaveSession(*savedLayout);
	}
	return true;
}

void Engine::EditorManager::RequestSaveAllEngineLayouts() {

	std::string error;
	if (!editorLayoutManager_.SaveAllEngineLayouts(error)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"Editor layout export failed: {}", error);
	}
}

void Engine::EditorManager::RequestApplyEditorLayout(const std::string& layoutID) {

	const EditorLayoutSnapshot* layout = editorLayoutManager_.FindLayout(layoutID);
	if (!layout) {
		return;
	}
	pendingEditorLayout_ = *layout;
	editorLayoutManager_.SetActiveLayoutID(layoutID);
}

void Engine::EditorManager::RequestDeleteEditorLayout(const std::string& layoutID) {

	const bool deletingActive = editorLayoutManager_.GetActiveLayoutID() == layoutID;
	if (!editorLayoutManager_.DeleteLayout(layoutID) || !deletingActive) {
		return;
	}

	if (const EditorLayoutSnapshot* defaultLayout = editorLayoutManager_.FindLayout(
		editorLayoutManager_.GetActiveLayoutID())) {
		pendingEditorLayout_ = *defaultLayout;
	}
}

void Engine::EditorManager::RequestImportEditorLayouts() {

	EditorLayoutSnapshot defaultLayout{};
	std::string error;
	if (editorLayoutManager_.ImportEngineLayouts(defaultLayout, error)) {
		pendingEditorLayout_ = std::move(defaultLayout);
	}
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

		// Z=0平面との交差判定(rd.zが0に近い場合は平行なのでスキップ)
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

		// グリフ矩形はピボット未適用なので、描画側と同じオフセットを加えて判定位置を合わせる
		// 正規化0-1基準のpivotがブロック全体のboundsSize上のこの点を原点へ寄せる
		const float pivotOffsetX = -renderer.pivot.x * renderer.runtimeLayout.boundsSize.x;
		const float pivotOffsetY = -renderer.pivot.y * renderer.runtimeLayout.boundsSize.y;
		minX += pivotOffsetX;
		maxX += pivotOffsetX;
		minY += pivotOffsetY;
		maxY += pivotOffsetY;

		if (hitPoint.x >= minX && hitPoint.x <= maxX &&
			hitPoint.y >= minY && hitPoint.y <= maxY) {
			hits.push_back({ entity, renderer.layer, renderer.order });
		}
	});

	if (hits.empty()) {
		return Entity::Null();
	}

	// レイヤーとオーダーの降順でソートし手前にあるものを優先する
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

	// クリック(ドラッグせず離した)時に選択を確定する、候補が未解決ならreadback到着時に確定する
	if (input->ReleaseMouse(MouseButton::Left)) {

		const bool overViewport = editorState_.sceneViewportHovered || editorState_.gameViewportHovered;
		if (overViewport && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {

			editorState_.scenePickClickPending = true;
			editorState_.scenePickClickAdditive = ImGui::IsKeyDown(ImGuiKey_LeftShift);
			// 即時候補や空クリックはreadback待ちが無ければその場で確定する
			if (!meshSubMeshPicker_->HasPendingReadback() && context.activeWorld) {
				editorState_.CommitScenePick(*context.activeWorld);
			}
		}
	}

	auto executePick = [&](InputViewArea inputArea, RenderViewKind viewKind,
		ID3D12Resource* tlasResource, const std::vector<MeshSubMeshPickRecord>& pickRecords) {

			// ビューの上で左クリックされたフレームのみ処理する
			if (!input->HasViewRect(inputArea)) {
				return false;
			}
			if (!input->IsMouseOnView(inputArea)) {
				return false;
			}
			// ビューのImageが最前面でホバーされている時だけ反応する
			// 他のImGuiウィンドウやポップアップが上にある状態での誤選択を防ぐ
			const bool viewportHovered = (viewKind == RenderViewKind::Game) ?
				editorState_.gameViewportHovered : editorState_.sceneViewportHovered;
			if (!viewportHovered) {
				return false;
			}
			if (!input->TriggerMouse(MouseButton::Left)) {
				return false;
			}
			// 押すたびに候補と保留クリックをリセットする、選択はリリースまで遅延する
			editorState_.scenePickDragEntity = Entity::Null();
			editorState_.scenePickClickPending = false;

			// マウス座標を取得
			const std::optional<Vector2> mousePosInView = input->GetMousePosInView(inputArea);
			if (!mousePosInView.has_value()) {
				return false;
			}

			// 即時ピック(2D/Overlay)はここで候補だけ設定し、選択はリリース時に確定する
			auto selectHit = [&](const Entity& hit) {
				editorState_.scenePickDragEntity = hit;
				editorState_.scenePickCandidateSubMesh = 0;
				editorState_.scenePickCandidateSubMeshID = UUID{};
				};

			// SceneView専用Overlayは通常2D/TLASより優先してEntity単位で選択する
			if (viewKind == RenderViewKind::Scene) {
				Entity overlayHit = Entity::Null();
				if (sceneComponentOverlayPicker_.Pick(context.activeWorld,
					renderPipeline.GetResolvedView(viewKind), mousePosInView.value(), overlayHit)) {
					selectHit(overlayHit);
					return true;
				}
			}

			// 2Dエンティティのピック処理を優先実行
			Entity hitEntity2D = Execute2DPick(mousePosInView.value(), renderPipeline.GetResolvedView(viewKind), context.activeWorld);
			if (hitEntity2D.IsValid()) {

				// 2Dが優先されるため、GPUによる3Dピックは行わず、即座に選択を確定する
				selectHit(hitEntity2D);
				return true;
			}

			// メッシュピック処理を実行、シフト状態は結果消費時のトグル判定に使う
			meshSubMeshPicker_->ExecutePick(graphicsCore, renderPipeline.GetResolvedView(viewKind),
				mousePosInView.value(), pickRecords, tlasResource, false, false);
			return true;
		};

	// SceneViewは従来通り、ギズモ操作中はピックしない
	if (layoutState_.showSceneView && !editorState_.useSceneGizmo) {
		if (executePick(InputViewArea::Scene, RenderViewKind::Scene,
			renderPipeline.GetSceneViewTLASResource(), renderPipeline.GetSceneViewPickRecords())) {
			return;
		}
	}

	// GameViewにもSceneViewと同じTLASピックだけを通し、マニピュレーターは表示しない
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
	const MeshSubMeshPickOutcome pickOutcome = meshSubMeshPicker_->ConsumePendingResult(context.activeWorld);
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

Engine::EditorLayoutSnapshot Engine::EditorManager::CaptureEditorLayout() const {

	EditorLayoutSnapshot layout{};
	layout.layoutID = editorLayoutManager_.GetActiveLayoutID();
	if (const EditorLayoutSnapshot* active = editorLayoutManager_.FindLayout(layout.layoutID)) {
		layout.displayName = active->displayName;
	}
	if (layout.displayName.empty()) {
		layout.displayName = "Current";
	}

	layout.visibility.showHierarchy = layoutState_.showHierarchy;
	layout.visibility.showInspector = layoutState_.showInspector;
	layout.visibility.showProject = layoutState_.showProject;
	layout.visibility.showConsole = layoutState_.showConsole;
	layout.visibility.showSceneView = layoutState_.showSceneView;
	layout.visibility.showGameView = layoutState_.showGameView;
	layout.visibility.showToolbar = layoutState_.showToolbar;
	layout.visibility.showTool = layoutState_.showTool;

	for (const auto& panel : panels_) {

		if (panel->GetPanelTypeID() != "Project" && panel->GetPanelTypeID() != "Inspector") {
			continue;
		}

		bool open = panel->IsInstanceOpen();
		if (panel->IsPrimaryInstance()) {
			open = panel->GetPanelTypeID() == "Project" ?
				layoutState_.showProject : layoutState_.showInspector;
		}
		layout.panels.push_back({
			.typeID = panel->GetPanelTypeID(),
			.instanceID = panel->GetInstanceID(),
			.primary = panel->IsPrimaryInstance(),
			.open = open,
			.state = panel->SaveLayoutState(),
			});
	}

	size_t iniSize = 0;
	const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
	if (iniData && iniSize != 0) {
		layout.imguiIniData.assign(iniData, iniSize);
	}
	return layout;
}

void Engine::EditorManager::ApplyEditorLayout(const EditorLayoutSnapshot& layout, GraphicsCore& graphicsCore) {

	layoutState_.showHierarchy = layout.visibility.showHierarchy;
	layoutState_.showInspector = layout.visibility.showInspector;
	layoutState_.showProject = layout.visibility.showProject;
	layoutState_.showConsole = layout.visibility.showConsole;
	layoutState_.showSceneView = layout.visibility.showSceneView;
	layoutState_.showGameView = layout.visibility.showGameView;
	layoutState_.showToolbar = layout.visibility.showToolbar;
	layoutState_.showTool = layout.visibility.showTool;

	// 適用前の複製パネルを破棄しスナップショットから作り直す
	panels_.erase(std::remove_if(panels_.begin(), panels_.end(), [](const std::unique_ptr<IEditorPanel>& panel) {
		return !panel->IsPrimaryInstance() && !panel->GetPanelTypeID().empty();
		}), panels_.end());

	EditorPanelCreateContext createContext{ graphicsCore.GetTextureUploadService() };
	for (const EditorPanelLayoutSnapshot& panelLayout : layout.panels) {

		if (panelLayout.typeID != "Project" && panelLayout.typeID != "Inspector") {
			continue;
		}

		if (panelLayout.primary) {

			auto found = std::find_if(panels_.begin(), panels_.end(), [&](const std::unique_ptr<IEditorPanel>& panel) {
				return panel->IsPrimaryInstance() && panel->GetPanelTypeID() == panelLayout.typeID;
				});
			if (found != panels_.end()) {
				(*found)->LoadLayoutState(panelLayout.state);
			}
			continue;
		}

		std::unique_ptr<IEditorPanel> panel = CreateBuiltinEditorPanelInstance(
			createContext, panelLayout.typeID, panelLayout.instanceID);
		if (!panel) {
			continue;
		}
		panel->SetInstanceOpen(panelLayout.open);
		panel->LoadLayoutState(panelLayout.state);
		panels_.emplace_back(std::move(panel));
	}

	ImGui::ClearIniSettings();
	if (!layout.imguiIniData.empty()) {
		ImGui::LoadIniSettingsFromMemory(layout.imguiIniData.c_str(), layout.imguiIniData.size());
		requestBuildDefaultDockLayout_ = false;
	} else {
		requestBuildDefaultDockLayout_ = layout.builtinDefault;
	}
}

void Engine::EditorManager::ApplyPendingEditorLayout(GraphicsCore& graphicsCore) {

	if (!pendingEditorLayout_) {
		return;
	}
	ApplyEditorLayout(pendingEditorLayout_.value(), graphicsCore);
	editorLayoutManager_.SaveSession(pendingEditorLayout_.value());
	pendingEditorLayout_.reset();
}

void Engine::EditorManager::ApplyPendingPanelDuplicate(const EditorPanelContext& context) {

	if (pendingDuplicatePanelID_.empty()) {
		return;
	}

	IEditorPanel* source = FindPanelByInstanceID(pendingDuplicatePanelID_);
	pendingDuplicatePanelID_.clear();
	if (!source || !source->CanDuplicate(context)) {
		return;
	}

	const std::string typeID = source->GetPanelTypeID();
	int32_t panelCount = 0;
	for (const auto& panel : panels_) {
		if (panel->GetPanelTypeID() == typeID) {
			++panelCount;
		}
	}

	const std::string instanceID = typeID + "." + ToString(UUID::New());
	const std::string displayName = typeID == "Project" ?
		"Project " + std::to_string(panelCount + 1) : std::string{};
	EditorPanelCreateContext createContext{ context.graphicsCore->GetTextureUploadService() };
	std::unique_ptr<IEditorPanel> duplicated = CreateBuiltinEditorPanelInstance(
		createContext, typeID, instanceID, displayName);
	if (!duplicated) {
		return;
	}

	duplicated->LoadLayoutState(source->MakeDuplicateState(context));
	duplicated->SetInitialDockID(source->GetCurrentDockID());
	panels_.emplace_back(std::move(duplicated));
	ImGui::GetIO().WantSaveIniSettings = true;
}

void Engine::EditorManager::RemoveClosedDuplicatedPanels() {

	panels_.erase(std::remove_if(panels_.begin(), panels_.end(), [](const std::unique_ptr<IEditorPanel>& panel) {
		return !panel->IsPrimaryInstance() && !panel->IsInstanceOpen();
		}), panels_.end());
}

Engine::IEditorPanel* Engine::EditorManager::FindPanelByInstanceID(const std::string& instanceID) const {

	const auto found = std::find_if(panels_.begin(), panels_.end(), [&](const std::unique_ptr<IEditorPanel>& panel) {
		return panel->GetInstanceID() == instanceID;
		});
	return found != panels_.end() ? found->get() : nullptr;
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

void Engine::EditorManager::LoadViewportPanelState() {

	const std::filesystem::path configPath = RuntimePaths::GetGameConfigPath(kViewportPanelStateConfigPath);
	if (!JsonAdapter::Check(configPath.string(), false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(configPath.string(), false);
	if (!data.is_object() || !data.contains("sceneView") || !data["sceneView"].is_object()) {
		return;
	}

	const nlohmann::json& sceneView = data["sceneView"];
	if (sceneView.contains("drawDefaultGrid") && sceneView["drawDefaultGrid"].is_boolean()) {
		editorState_.drawSceneViewDefaultGrid = sceneView["drawDefaultGrid"].get<bool>();
	}
	if (sceneView.contains("enableSnapEditEntity") && sceneView["enableSnapEditEntity"].is_boolean()) {
		editorState_.enableSnapEditEntity = sceneView["enableSnapEditEntity"].get<bool>();
	}
	// ギズモのスナップ設定を読み込む
	if (sceneView.contains("snap") && sceneView["snap"].is_object()) {

		const nlohmann::json& snap = sceneView["snap"];
		auto loadAxis = [&snap](const char* key, GridSnapAxis& axis) {
			if (!snap.contains(key) || !snap[key].is_object()) {
				return;
			}
			const nlohmann::json& a = snap[key];
			if (a.contains("size") && a["size"].is_number()) {
				axis.size = a["size"].get<float>();
			}
			if (a.contains("absolute") && a["absolute"].is_boolean()) {
				axis.absolute = a["absolute"].get<bool>();
			}
			};
		EntitySnapSettings& s = editorState_.snapSettings;
		loadAxis("translate2D", s.translate2D);
		loadAxis("rotate2D", s.rotate2D);
		loadAxis("scale2D", s.scale2D);
		loadAxis("translate3D", s.translate3D);
		loadAxis("rotate3D", s.rotate3D);
		loadAxis("scale3D", s.scale3D);
		if (snap.contains("drawGrid") && snap["drawGrid"].is_boolean()) {
			s.drawSnapGrid = snap["drawGrid"].get<bool>();
		}
	}

	LoadEnumValue(sceneView, "manipulatorMode", editorState_.sceneViewManipulatorMode);
	LoadEnumValue(sceneView, "cameraMode", editorState_.sceneViewCamera.mode);
	LoadEnumValue(sceneView, "manualCameraDimension", editorState_.manualCameraDimension);

	// 実体参照は起動時に持ち越さずモードだけを復元しカメラ指定は現在のシーンで選び直す
	editorState_.sceneViewCamera.ClearAssignedCameras();
	editorState_.ClearSelection();
}

void Engine::EditorManager::SaveViewportPanelState() const {

	nlohmann::json sceneView = nlohmann::json::object();
	sceneView["drawDefaultGrid"] = editorState_.drawSceneViewDefaultGrid;
	sceneView["enableSnapEditEntity"] = editorState_.enableSnapEditEntity;
	// ギズモのスナップ設定を書き出す
	{
		auto saveAxis = [](const GridSnapAxis& axis) {
			return nlohmann::json{ { "size", axis.size }, { "absolute", axis.absolute } };
			};
		const EntitySnapSettings& s = editorState_.snapSettings;
		nlohmann::json snap = nlohmann::json::object();
		snap["translate2D"] = saveAxis(s.translate2D);
		snap["rotate2D"] = saveAxis(s.rotate2D);
		snap["scale2D"] = saveAxis(s.scale2D);
		snap["translate3D"] = saveAxis(s.translate3D);
		snap["rotate3D"] = saveAxis(s.rotate3D);
		snap["scale3D"] = saveAxis(s.scale3D);
		snap["drawGrid"] = s.drawSnapGrid;
		sceneView["snap"] = snap;
	}
	sceneView["manipulatorMode"] = EnumAdapter<SceneViewManipulatorMode>::ToString(editorState_.sceneViewManipulatorMode);
	sceneView["cameraMode"] = EnumAdapter<SceneViewCameraMode>::ToString(editorState_.sceneViewCamera.mode);
	sceneView["manualCameraDimension"] = EnumAdapter<Dimension>::ToString(editorState_.manualCameraDimension);

	nlohmann::json data = nlohmann::json::object();
	data["sceneView"] = sceneView;

	const std::filesystem::path configPath = RuntimePaths::GetGameConfigPath(kViewportPanelStateConfigPath);
	JsonAdapter::Save(configPath.string(), data);
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
