#include "ViewportPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/Commands/Transform/SetTransformCommand.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

//============================================================================
//	ViewportPanel classMethods
//============================================================================
namespace {

	bool Prefers2DGizmo(const Engine::EditorPanelContext& context,
		Engine::ECSWorld& world, const Engine::Entity& entity) {

		// 2D描画に関係するコンポーネントがある場合は2Dギズモを優先
		if (world.HasComponent<Engine::SpriteRendererComponent>(entity) ||
			world.HasComponent<Engine::TextRendererComponent>(entity) ||
			world.HasComponent<Engine::OrthographicCameraComponent>(entity)) {
			return true;
		}
		if (world.HasComponent<Engine::MeshRendererComponent>(entity) ||
			world.HasComponent<Engine::PerspectiveCameraComponent>(entity) ||
			world.HasComponent<Engine::DirectionalLightComponent>(entity) ||
			world.HasComponent<Engine::PointLightComponent>(entity) ||
			world.HasComponent<Engine::SpotLightComponent>(entity)) {
			return false;
		}
		return context.editorState && context.editorState->manualCameraDimension == Engine::Dimension::Type2D;
	}
	// シーンギズモの描画に使用するカメラビューを選択する
	const Engine::ResolvedCameraView* SelectSceneGizmoCamera(const Engine::ResolvedRenderView& view, bool prefer2DTarget) {

		if (prefer2DTarget) {
			if (view.orthographic.valid) {
				return &view.orthographic;
			}
			if (view.perspective.valid) {
				return &view.perspective;
			}
		} else {
			if (view.perspective.valid) {
				return &view.perspective;
			}
			if (view.orthographic.valid) {
				return &view.orthographic;
			}
		}
		return nullptr;
	}
	// エンティティの親のワールド行列を取得する
	Engine::Matrix4x4 GetEntityParentWorldMatrix(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return Engine::Matrix4x4::Identity();
		}
		const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
		if (!world.IsAlive(hierarchy.parent) || !world.HasComponent<Engine::TransformComponent>(hierarchy.parent)) {

			return Engine::Matrix4x4::Identity();
		}
		return world.GetComponent<Engine::TransformComponent>(hierarchy.parent).worldMatrix;
	}
	// HierarchyPanelと同じEntity payloadをViewportからも送る
	void DrawViewportEntityDragDropSource(const Engine::EditorPanelContext& context, bool blockByGizmo) {

		if (blockByGizmo || !context.editorState || !context.editorState->enableScenePick) {
			return;
		}

		Engine::ECSWorld* world = context.GetWorld();
		if (!world || !world->IsAlive(context.editorState->selectedEntity)) {
			return;
		}

		// Viewport上ではクリックで選択したEntityを、そのまま他UIへドラッグできるようにする
		// hover中だけに限定すると、ドロップ先へ移動した瞬間にSource描画が切れて"..."表示になる
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

			const Engine::Entity entity = context.editorState->selectedEntity;
			const Engine::UUID stableUUID = world->GetUUID(entity);
			ImGui::SetDragDropPayload(Engine::IEditorPanel::kHierarchyDragDropPayloadType, &stableUUID, sizeof(Engine::UUID));

			std::string displayName = "Entity";
			if (world->HasComponent<Engine::NameComponent>(entity)) {
				displayName = world->GetComponent<Engine::NameComponent>(entity).name;
			}
			if (displayName.empty()) {
				displayName = "Entity";
			}
			ImGui::Text("%s", displayName.c_str());
			ImGui::EndDragDropSource();
		}
	}

	// カメラ選択の候補
	struct CameraChoice {

		Engine::UUID uuid{};
		std::string label;
	};

	// カメラ選択肢のラベルを作る
	std::string MakeCameraLabel(Engine::ECSWorld& world, Engine::Entity entity, const char* suffix) {

		std::string name = "Entity";
		if (world.HasComponent<Engine::NameComponent>(entity)) {
			name = world.GetComponent<Engine::NameComponent>(entity).name;
		}
		name += " [";
		name += suffix;
		name += "]";
		return name;
	}
	// ワールドからカメラ選択肢を集める
	template <typename TCamera>
	std::vector<CameraChoice> CollectCameraChoices(Engine::ECSWorld& world, const char* suffix) {

		std::vector<CameraChoice> result{};
		// カメラコンポーネントを持つ全てのエンティティに対して処理
		world.ForEach<TCamera>([&](Engine::Entity entity, TCamera& camera) {

			if (!world.IsAlive(entity)) {
				return;
			}
			if (!camera.common.enabled) {
				return;
			}
			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			CameraChoice choice{};
			choice.uuid = world.GetUUID(entity);
			choice.label = MakeCameraLabel(world, entity, suffix);
			result.push_back(std::move(choice));
			});
		std::sort(result.begin(), result.end(),
			[](const CameraChoice& lhs, const CameraChoice& rhs) {
				return lhs.label < rhs.label;
			});
		return result;
	}
	// カメラ選択肢から現在の選択のプレビュー文字列を見つける
	std::string FindChoicePreview(const std::vector<CameraChoice>& choices,
		Engine::UUID currentUUID, const char* fallbackLabel) {

		if (!currentUUID) {
			return fallbackLabel;
		}
		for (const auto& choice : choices) {
			if (choice.uuid == currentUUID) {
				return choice.label;
			}
		}
		return fallbackLabel;
	}
	// カメラ選択コンボボックスを描画する
	void DrawCameraChoiceCombo(const char* label, Engine::UUID& currentUUID,
		const std::vector<CameraChoice>& choices, const char* autoLabel) {

		std::string preview = FindChoicePreview(choices, currentUUID, autoLabel);

		if (!ImGui::BeginCombo(label, preview.c_str())) {
			return;
		}

		bool autoSelected = !currentUUID;
		if (ImGui::Selectable(autoLabel, autoSelected)) {
			currentUUID = Engine::UUID{};
		}
		if (autoSelected) {
			ImGui::SetItemDefaultFocus();
		}

		for (const auto& choice : choices) {
			const bool isSelected = (choice.uuid == currentUUID);
			if (ImGui::Selectable(choice.label.c_str(), isSelected)) {
				currentUUID = choice.uuid;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

Engine::ViewportPanel::ViewportPanel(const char* windowName, const char* label, ViewportPanelKind kind, TextureUploadService& textureUploadService) :
	windowName_(windowName), label_(label), kind_(kind), textureUploadService_(&textureUploadService) {

	icons_.enablePickKey = "enablePickKey.dds";
	icons_.noneKey = "manipulatorNone.dds";
	icons_.translateKey = "manipulatorTranslate.dds";
	icons_.rotateKey = "manipulatorRotate.dds";
	icons_.scaleKey = "manipulatorScale.dds";
	icons_.debugCameraKey = "debugCamera.dds";
	icons_.entityCameraKey = "entityCamera.dds";
	icons_.entitySelectKey = "entitySelect.dds";
	icons_.subMeshSelectKey = "subMeshSelect.dds";
	icons_.manualCamera2DKey = "sceneCameraMode2D.dds";
	icons_.manualCamera3DKey = "sceneCameraMode3D.dds";

	// アイコンの読み込み要求
	RequestIcons();
}

void Engine::ViewportPanel::Draw(const EditorPanelContext& context) {

	bool* visible = nullptr;
	ImVec2* lastSize = nullptr;
	switch (kind_) {
	case ViewportPanelKind::Scene:

		visible = &context.layoutState->showSceneView;
		lastSize = &context.layoutState->lastSceneViewSize;
		break;
	case ViewportPanelKind::Game:

		visible = &context.layoutState->showGameView;
		lastSize = &context.layoutState->lastGameViewSize;
		break;
	}
	// 無効な種類の場合は何もしない
	if (!visible) {
		return;
	}

	if (!ImGui::Begin(windowName_.c_str(), visible)) {
		ImGui::End();
		return;
	}

	// ビューポートの内容を描画する
	*lastSize = ImGui::GetContentRegionAvail();
	DrawViewportContent(context, label_.c_str(), *lastSize);

	ImGui::End();
}

void Engine::ViewportPanel::DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size) {

	// 描画領域のサイズが小さすぎる場合は、最小サイズを1x1にする
	ImVec2 region = size;

	ImGui::BeginChild(id, region, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// ビューポートの種類に応じた描画ビューのサーフェスを取得
	RenderViewKind viewKind = (kind_ == ViewportPanelKind::Game) ? RenderViewKind::Game : RenderViewKind::Scene;
	InputViewArea inputArea = (kind_ == ViewportPanelKind::Game) ? InputViewArea::Game : InputViewArea::Scene;

	if (const RenderTexture2D* display = context.viewportRenderService->GetDisplayTexture(viewKind)) {

		// 表示サイズ
		Vector2 srcSize(static_cast<float>(display->GetRenderTarget().width), static_cast<float>(display->GetRenderTarget().height));

		// 表示ウィンドウの中心に16:9で表示させる
		ImVec2 avail = ImGui::GetContentRegionAvail();
		const float aspect = 16.0f / 9.0f;
		if (avail.x / avail.y >= aspect) {
			viewSize_.y = avail.y;
			viewSize_.x = avail.y * aspect;
		} else {
			viewSize_.x = avail.x;
			viewSize_.y = avail.x / aspect;
		}

		// シーンビューの場合は左側にツールボタンを表示
		if (kind_ == ViewportPanelKind::Scene) {

			ImGui::BeginGroup();
			DrawManipulatorSection(context);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			DrawCameraSection(context);
			DrawEntityCameraPopup(context);
			ImGui::EndGroup();
			ImGui::SameLine();

			// ツールボタンの幅を除いた残りの領域で中央寄せ
			avail = ImGui::GetContentRegionAvail();
			if (avail.x / avail.y >= aspect) {
				viewSize_.y = avail.y;
				viewSize_.x = avail.y * aspect;
			} else {
				viewSize_.x = avail.x;
				viewSize_.y = avail.x / aspect;
			}
		}

		ImGui::SetCursorPosX(ImGui::GetCursorPos().x + (avail.x - viewSize_.x) * 0.5f);
		ImGui::SetCursorPosY(ImGui::GetCursorPos().y + (avail.y - viewSize_.y) * 0.5f);

		// 実際にImageを置く位置を入力システムへ渡す
		const ImVec2 imagePos = ImGui::GetCursorScreenPos();
		Input::GetInstance()->SetViewRect(inputArea, Vector2(imagePos.x, imagePos.y),
			Vector2(viewSize_.x, viewSize_.y), srcSize);

		// 描画ビューのサーフェスをImGuiに描画
		ImGui::Image(static_cast<ImTextureID>(display->GetSRVGPUHandle().ptr), viewSize_);

		// シーンビューの場合はシーンギズモも描画
		bool blockDragByGizmo = false;
		if (kind_ == ViewportPanelKind::Scene) {

			DrawSceneGizmo(context);
			blockDragByGizmo = context.editorState && context.editorState->useSceneGizmo;
		}
		DrawViewportEntityDragDropSource(context, blockDragByGizmo);
	}
	ImGui::EndChild();
}

void Engine::ViewportPanel::DrawSceneGizmo(const EditorPanelContext& context) {

	// フラグリセット
	context.editorState->useSceneGizmo = false;

	ECSWorld* worldPtr = context.GetWorld();
	if (!worldPtr) {
		return;
	}
	ECSWorld& world = *worldPtr;

	// 編集不可の場合はギズモセッションを終了して何もしない
	if (!context.CanEditScene() || !context.sceneRenderView || !context.sceneRenderView->valid ||
		context.editorState->sceneViewManipulatorMode == SceneViewManipulatorMode::None) {
		FinalizeEntityGizmoSession(context, world);
		return;
	}
	// シーンビューのマニピュレーター選択が「なし」の場合はギズモセッションを終了して何もしない
	if (context.editorState->sceneViewManipulatorMode == SceneViewManipulatorMode::None) {
		FinalizeEntityGizmoSession(context, world);
		return;
	}

	// 現在のマニピュレーター操作を取得
	const ImVec2 rectMin = ImGui::GetItemRectMin();
	const ImVec2 rectMax = ImGui::GetItemRectMax();

	// ギズモの描画に必要な情報をまとめた構造体を作成
	GizmoViewportRect rect{};
	rect.x = rectMin.x;
	rect.y = rectMin.y;
	rect.width = rectMax.x - rectMin.x;
	rect.height = rectMax.y - rectMin.y;
	// 描画領域が有効でない場合はギズモセッションを終了して何もしない
	if (!rect.IsValid()) {
		FinalizeEntityGizmoSession(context, world);
		return;
	}

	//============================================================================
	//	エンティティ選択中
	//============================================================================
	{
		const Entity entity = context.editorState->selectedEntity;
		// 編集不可なエンティティの場合はギズモセッションを終了して何もしない
		if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
			FinalizeEntityGizmoSession(context, world);
			return;
		}
		// 別セッションなら先に閉じる
		if (entityGizmoSession_.active && entityGizmoSession_.entityUUID != world.GetUUID(entity)) {
			FinalizeEntityGizmoSession(context, world);
		}

		// 現在のマニピュレーター操作を取得
		bool use2DTarget = Prefers2DGizmo(context, world, entity);
		const ResolvedCameraView* camera = SelectSceneGizmoCamera(*context.sceneRenderView, use2DTarget);
		if (!camera) {
			FinalizeEntityGizmoSession(context, world);
			return;
		}

		// ギズモの描画に必要な情報をまとめた構造体を作成
		GizmoViewContext gizmoContext{};
		gizmoContext.rect = rect;
		gizmoContext.viewMatrix = camera->matrices.viewMatrix;
		gizmoContext.projectionMatrix = camera->matrices.projectionMatrix;
		gizmoContext.parentWorldMatrix = GetEntityParentWorldMatrix(world, entity);
		gizmoContext.mode = context.editorState->sceneViewManipulatorMode;
		gizmoContext.orthographic = camera == &context.sceneRenderView->orthographic;
		gizmoContext.allowAxisFlip = !use2DTarget;

		TransformComponent previewTransform = world.GetComponent<TransformComponent>(entity);

		// ギズモを描画し、操作結果を取得する
		const GizmoEditResult result = use2DTarget ? MyGUI::Manipulate2D("##SceneEntityGizmo2D", gizmoContext, previewTransform) :
			MyGUI::Manipulate3D("##SceneEntityGizmo3D", gizmoContext, previewTransform);

		// 使用しているか
		context.editorState->useSceneGizmo = result.IsUse();

		if (result.isUsing && !entityGizmoSession_.active) {

			entityGizmoSession_.active = true;
			entityGizmoSession_.entityUUID = world.GetUUID(entity);
			entityGizmoSession_.beforeTransform = world.GetComponent<TransformComponent>(entity);
		}

		// 値が変更された場合はプレビュー設定をエンティティに適用する
		if (result.valueChanged) {

			TransformEditUtility::ApplyImmediate(world, entity, previewTransform);
		}
		// 使用を終了した場合はセッションを終了する
		if (entityGizmoSession_.active && !result.isUsing) {

			FinalizeEntityGizmoSession(context, world);
		}
	}
}

void Engine::ViewportPanel::FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world) {

	if (!entityGizmoSession_.active) {
		return;
	}

	if (context.host) {
		const Entity entity = world.FindByUUID(entityGizmoSession_.entityUUID);
		if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {

			const TransformComponent afterTransform = world.GetComponent<TransformComponent>(entity);
			// トランスフォームの値が変更されている場合はコマンドを実行して変更を記録する
			if (!SetTransformCommand::NearlyEqualTransform(entityGizmoSession_.beforeTransform, afterTransform)) {

				context.host->ExecuteEditorCommand(std::make_unique<SetTransformCommand>(entity,
					entityGizmoSession_.beforeTransform, afterTransform));
			}
		}
	}
	entityGizmoSession_ = {};
}

void Engine::ViewportPanel::RequestIcons() {

	if (!textureUploadService_) {
		return;
	}

	textureUploadService_->RequestTextureFile(icons_.enablePickKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.enablePickKey));
	textureUploadService_->RequestTextureFile(icons_.noneKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.noneKey));
	textureUploadService_->RequestTextureFile(icons_.translateKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.translateKey));
	textureUploadService_->RequestTextureFile(icons_.rotateKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.rotateKey));
	textureUploadService_->RequestTextureFile(icons_.scaleKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.scaleKey));
	textureUploadService_->RequestTextureFile(icons_.debugCameraKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.debugCameraKey));
	textureUploadService_->RequestTextureFile(icons_.entityCameraKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.entityCameraKey));
	textureUploadService_->RequestTextureFile(icons_.entitySelectKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.entitySelectKey));
	textureUploadService_->RequestTextureFile(icons_.subMeshSelectKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.subMeshSelectKey));
	textureUploadService_->RequestTextureFile(icons_.manualCamera2DKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.manualCamera2DKey));
	textureUploadService_->RequestTextureFile(icons_.manualCamera3DKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.manualCamera3DKey));
}

ImTextureID Engine::ViewportPanel::GetTextureID(const std::string& key) const {

	if (!textureUploadService_) {
		return ImTextureID{};
	}
	return EditorTextureHelper::GetImTextureID(*textureUploadService_, key);
}

void Engine::ViewportPanel::DrawCameraSection(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	SceneViewCameraSelection& selection = context.editorState->sceneViewCamera;

	// デバッグカメラを選ぶボタン
	if (DrawIconButton("##SceneCameraDebug", GetTextureID(icons_.debugCameraKey),
		selection.mode == SceneViewCameraMode::DebugManual, buttonSize_)) {

		selection.mode = SceneViewCameraMode::DebugManual;
		selection.ClearAssignedCameras();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("マニュアルカメラ操作");
	}
	// 選択エンティティのカメラを選ぶボタン
	if (DrawIconButton("##SceneCameraSelected", GetTextureID(icons_.entityCameraKey),
		selection.mode == SceneViewCameraMode::SelectedEntityCamera, buttonSize_)) {

		selection.mode = SceneViewCameraMode::SelectedEntityCamera;
		ImGui::OpenPopup("##SceneEntityCameraPopup");
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("エンティティカメラによる制御");
	}

	// マニュアルカメラの次元を切り替えるボタン
	Dimension& operationMode = context.editorState->manualCameraDimension;

	bool is3D = (operationMode == Dimension::Type3D);
	// アイコンは次元に応じて変える
	ImTextureID modeIcon = is3D ? GetTextureID(icons_.manualCamera3DKey) :
		GetTextureID(icons_.manualCamera2DKey);
	if (DrawIconButton("##SceneDimensionMode", modeIcon, true, buttonSize_)) {

		operationMode = is3D ? Dimension::Type2D : Dimension::Type3D;
	}
	if (ImGui::IsItemHovered()) {

		std::string tooltip = std::string("マニュアルカメラの2D/3D切り替え\n現在の状態: ") +
			(operationMode == Dimension::Type2D ? "2D" : "3D");
		ImGui::SetTooltip("%s", tooltip.c_str());
	}
}

void Engine::ViewportPanel::DrawManipulatorSection(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	SceneViewManipulatorMode& mode = context.editorState->sceneViewManipulatorMode;

	if (DrawIconButton("##EnablePickKey", GetTextureID(icons_.enablePickKey),
		!context.editorState->enableScenePick, buttonSize_)) {

		context.editorState->enableScenePick = !context.editorState->enableScenePick;
	}
	if (ImGui::IsItemHovered()) {

		std::string tooltip = std::string("シーンオブジェクト選択の有効/無効切り替え\n現在の状態: ") +
			(context.editorState->enableScenePick ? "有効" : "無効");
		ImGui::SetTooltip("%s", tooltip.c_str());
	}
	if (DrawIconButton("##ManipulatorNone", GetTextureID(icons_.noneKey),
		mode == SceneViewManipulatorMode::None, buttonSize_)) {

		mode = SceneViewManipulatorMode::None;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("マニュピレーター表示なし");
	}
	if (DrawIconButton("##ManipulatorTranslate", GetTextureID(icons_.translateKey),
		mode == SceneViewManipulatorMode::Translate, buttonSize_)) {

		mode = SceneViewManipulatorMode::Translate;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("座標編集");
	}
	if (DrawIconButton("##ManipulatorRotate", GetTextureID(icons_.rotateKey),
		mode == SceneViewManipulatorMode::Rotate, buttonSize_)) {

		mode = SceneViewManipulatorMode::Rotate;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("回転編集");
	}
	if (DrawIconButton("##ManipulatorScale", GetTextureID(icons_.scaleKey),
		mode == SceneViewManipulatorMode::Scale, buttonSize_)) {

		mode = SceneViewManipulatorMode::Scale;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("拡縮編集");
	}

	EditorSelectionKind& kind = context.editorState->selectKind;

	// エンティティ/サブメッシュ選択モードの切り替え
	if (kind == EditorSelectionKind::Entity) {
		if (DrawIconButton("##SelectEntity", GetTextureID(icons_.entitySelectKey), true, buttonSize_)) {

			kind = EditorSelectionKind::MeshSubMesh;
		}
	} else {
		if (DrawIconButton("##SelectSubMesh", GetTextureID(icons_.subMeshSelectKey), true, buttonSize_)) {

			kind = EditorSelectionKind::Entity;
		}
	}
	if (ImGui::IsItemHovered()) {

		std::string tooltip = std::string("選択対象の切り替え\n現在の対象: ") +
			(kind == EditorSelectionKind::Entity ? "エンティティ単位" : "サブメッシュ単位");
		ImGui::SetTooltip("%s", tooltip.c_str());
	}
}

bool Engine::ViewportPanel::DrawIconButton(const char* id, ImTextureID textureID, bool active, const ImVec2& size) const {

	const ImVec4 normal = active
		? ImVec4(0.05f, 0.18f, 0.45f, 1.00f)   // active: dark deep blue
		: ImVec4(0.04f, 0.04f, 0.04f, 0.95f);  // inactive: near black

	const ImVec4 hovered = active
		? ImVec4(0.07f, 0.24f, 0.58f, 1.00f)   // active hover: slightly brighter dark blue
		: ImVec4(0.08f, 0.08f, 0.08f, 0.98f);  // inactive hover

	const ImVec4 pressed = active
		? ImVec4(0.10f, 0.32f, 0.74f, 1.00f)   // active pressed: stronger blue
		: ImVec4(0.02f, 0.02f, 0.02f, 1.00f);  // inactive pressed

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, normal);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, pressed);

	bool result = ImGui::ImageButton(id, textureID, size, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar();

	if (active) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
			IM_COL32(26, 82, 190, 255), 4.0f, 0, 2.0f);
	}
	return result;
}

void Engine::ViewportPanel::DrawEntityCameraPopup(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return;
	}

	SceneViewCameraSelection& selection = context.editorState->sceneViewCamera;
	std::vector<CameraChoice> orthoChoices = CollectCameraChoices<OrthographicCameraComponent>(*world, "2D");
	std::vector<CameraChoice> perspChoices = CollectCameraChoices<PerspectiveCameraComponent>(*world, "3D");

	if (!ImGui::BeginPopup("##SceneEntityCameraPopup")) {
		return;
	}

	ImGui::PushItemWidth(256.0f);
	DrawCameraChoiceCombo("2D Camera", selection.orthographicCameraUUID, orthoChoices, "<Auto 2D>");
	DrawCameraChoiceCombo("3D Camera", selection.perspectiveCameraUUID, perspChoices, "<Auto 3D>");
	ImGui::PopItemWidth();

	ImGui::Separator();

	if (ImGui::Button("Clear")) {

		selection.ClearAssignedCameras();
	}
	ImGui::SameLine();
	if (ImGui::Button("Use Debug Camera")) {

		selection.mode = SceneViewCameraMode::DebugManual;
		selection.ClearAssignedCameras();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

