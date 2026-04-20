#include "SceneViewToolPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/CameraComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>

//============================================================================
//	SceneViewToolPanel classMethods
//============================================================================

namespace {

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

Engine::SceneViewToolPanel::SceneViewToolPanel(TextureUploadService& textureUploadService) :
	textureUploadService_(&textureUploadService) {

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

void Engine::SceneViewToolPanel::RequestIcons() {

	if (!textureUploadService_) {
		return;
	}

	const std::string basePath = "Engine/Assets/Textures/Engine/Editor/Tool/";

	textureUploadService_->RequestTextureFile(icons_.enablePickKey, basePath + icons_.enablePickKey);
	textureUploadService_->RequestTextureFile(icons_.noneKey, basePath + icons_.noneKey);
	textureUploadService_->RequestTextureFile(icons_.translateKey, basePath + icons_.translateKey);
	textureUploadService_->RequestTextureFile(icons_.rotateKey, basePath + icons_.rotateKey);
	textureUploadService_->RequestTextureFile(icons_.scaleKey, basePath + icons_.scaleKey);
	textureUploadService_->RequestTextureFile(icons_.debugCameraKey, basePath + icons_.debugCameraKey);
	textureUploadService_->RequestTextureFile(icons_.entityCameraKey, basePath + icons_.entityCameraKey);
	textureUploadService_->RequestTextureFile(icons_.entitySelectKey, basePath + icons_.entitySelectKey);
	textureUploadService_->RequestTextureFile(icons_.subMeshSelectKey, basePath + icons_.subMeshSelectKey);
	textureUploadService_->RequestTextureFile(icons_.manualCamera2DKey, basePath + icons_.manualCamera2DKey);
	textureUploadService_->RequestTextureFile(icons_.manualCamera3DKey, basePath + icons_.manualCamera3DKey);
}

ImTextureID Engine::SceneViewToolPanel::GetTextureID(const std::string& key) const {

	if (const GPUTextureResource* texture = textureUploadService_->GetTexture(key)) {
		return static_cast<ImTextureID>(texture->gpuHandle.ptr);
	}
	return ImTextureID{};
}

void Engine::SceneViewToolPanel::Draw(const EditorPanelContext& context) {

	// インスペクターパネルの表示状態を確認
	if (!context.layoutState || !context.layoutState->showSceneView) {
		return;
	}

	if (!ImGui::Begin("ViewTool", nullptr, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	// マニピュレーターセクション
	DrawManipulatorSection(context);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// カメラセクション
	DrawCameraSection(context);
	ImGui::SetWindowFontScale(0.8f);
	DrawEntityCameraPopup(context);
	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
}

void Engine::SceneViewToolPanel::DrawCameraSection(const EditorPanelContext& context) {

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

void Engine::SceneViewToolPanel::DrawManipulatorSection(const EditorPanelContext& context) {

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

bool Engine::SceneViewToolPanel::DrawIconButton(const char* id, ImTextureID textureID, bool active, const ImVec2& size) const {

	const ImVec4 normal = active ? ImVec4(0.20f, 0.38f, 0.78f, 1.00f) : ImVec4(0.16f, 0.16f, 0.18f, 0.92f);
	const ImVec4 hovered = active ? ImVec4(0.25f, 0.45f, 0.88f, 1.00f) : ImVec4(0.22f, 0.22f, 0.25f, 0.95f);
	const ImVec4 pressed = active ? ImVec4(0.16f, 0.30f, 0.88f, 1.00f) : ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

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
			IM_COL32(128, 128, 255, 255), 4.0f, 0, 2.0f);
	}
	return result;
}

void Engine::SceneViewToolPanel::DrawEntityCameraPopup(const EditorPanelContext& context) {

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