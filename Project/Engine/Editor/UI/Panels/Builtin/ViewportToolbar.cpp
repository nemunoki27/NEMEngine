#include "ViewportToolbar.h"
#include "ViewportCameraSelection.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

namespace {

	// ツール列に区切りを表示する
	void DrawToolSeparator(const ImVec2& buttonSize) {

		ImGui::Spacing();

		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddLine(
			pos, ImVec2(pos.x + buttonSize.x + buttonSize.x / 2.0f, pos.y), ImGui::GetColorU32(ImGuiCol_Separator));
		ImGui::Dummy(ImVec2(buttonSize.x, 1.0f));

		ImGui::Spacing();
	}

	// 操作状態に合わせてButtonの色を切り替える
	bool DrawIconButton(const char* id, ImTextureID textureID, bool active, const ImVec2& size) {

		const ImVec4 normal = active ? ImVec4(0.05f, 0.18f, 0.45f, 1.00f) : ImVec4(0.04f, 0.04f, 0.04f, 0.95f);

		const ImVec4 hovered = active ? ImVec4(0.07f, 0.24f, 0.58f, 1.00f) : ImVec4(0.08f, 0.08f, 0.08f, 0.98f);

		const ImVec4 pressed = active ? ImVec4(0.10f, 0.32f, 0.74f, 1.00f) : ImVec4(0.02f, 0.02f, 0.02f, 1.00f);

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
			drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(26, 82, 190, 255), 4.0f, 0, 2.0f);
		}
		return result;
	}
}

//============================================================================
//	ViewportToolbar classMethods
//============================================================================

Engine::ViewportToolbar::ViewportToolbar(TextureUploadService& textureUploadService)
	: textureUploadService_(textureUploadService) {

	RequestIcons();
}

void Engine::ViewportToolbar::Draw(const EditorPanelContext& context) {

	ImGui::BeginGroup();
	// Prefabの表示範囲を切り替える
	if (context.editorContext && context.editorContext->isPrefabEditing) {

		const bool inContextActive = context.editorContext->isPrefabInContext;
		if (DrawIconButton("##TogglePrefabInContext", GetTextureID(icons_.prefabExitKey), inContextActive, buttonSize_) &&
			context.host) {
			context.host->RequestTogglePrefabInContext();
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip = std::string("In-Context編集の切り替え\nオンで元シーンに置いて編集\n現在: ") +
								  (inContextActive ? "オン" : "オフ");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
		DrawToolSeparator(buttonSize_);
	}

	DrawManipulatorSection(context);
	DrawToolSeparator(buttonSize_);
	DrawCameraSection(context);
	DrawToolSeparator(buttonSize_);
	DrawGridSection(context);
	ViewportCameraSelection::DrawPopup(context);
	ImGui::EndGroup();
}

void Engine::ViewportToolbar::RequestIcons() {

	// 同じ順でアイコンの読込を要求する
	const std::string* keys[] = {
		&icons_.enablePickKey,
		&icons_.noneKey,
		&icons_.translateKey,
		&icons_.rotateKey,
		&icons_.scaleKey,
		&icons_.debugCameraKey,
		&icons_.entityCameraKey,
		&icons_.entitySelectKey,
		&icons_.subMeshSelectKey,
		&icons_.selection2DKey,
		&icons_.selection3DKey,
		&icons_.selection2DAnd3DKey,
		&icons_.drawGridKey,
		&icons_.gizmoCenterPivotKey,
		&icons_.eachEntityOriginKey,
		&icons_.snapEditEntityKey,
		&icons_.prefabExitKey,
	};
	for (const std::string* key : keys) {
		textureUploadService_.RequestTextureFile(*key, EditorTextureHelper::MakeEditorTexturePath("Tool", *key));
	}
}

ImTextureID Engine::ViewportToolbar::GetTextureID(const std::string& key) const {

	return EditorTextureHelper::GetImTextureID(textureUploadService_, key);
}

void Engine::ViewportToolbar::DrawCameraSection(const EditorPanelContext& context) {

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
}

void Engine::ViewportToolbar::DrawManipulatorSection(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	SceneViewManipulatorMode& mode = context.editorState->sceneViewManipulatorMode;
	// Entity選択を切り替える
	{
		if (DrawIconButton(
				"##EnablePickKey", GetTextureID(icons_.enablePickKey), !context.editorState->enableScenePick, buttonSize_)) {

			context.editorState->enableScenePick = !context.editorState->enableScenePick;
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip = std::string("シーンオブジェクト選択の有効/無効切り替え\n現在の状態: ") +
								  (context.editorState->enableScenePick ? "有効" : "無効");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}

		// 選択対象の次元を順に切り替える
		SceneViewPickDimension& pickDimension = context.editorState->sceneViewPickDimension;
		ImTextureID dimensionIcon = GetTextureID(icons_.selection3DKey);
		if (pickDimension == SceneViewPickDimension::Type2D) {
			dimensionIcon = GetTextureID(icons_.selection2DKey);
		} else if (pickDimension == SceneViewPickDimension::Both) {
			dimensionIcon = GetTextureID(icons_.selection2DAnd3DKey);
		}
		if (DrawIconButton("##ScenePickDimension", dimensionIcon, true, buttonSize_)) {

			switch (pickDimension) {
			case SceneViewPickDimension::Type3D:
				pickDimension = SceneViewPickDimension::Type2D;
				break;
			case SceneViewPickDimension::Type2D:
				pickDimension = SceneViewPickDimension::Both;
				break;
			case SceneViewPickDimension::Both:
				pickDimension = SceneViewPickDimension::Type3D;
				break;
			}
		}
		if (ImGui::IsItemHovered()) {

			const char* dimensionLabel = "3Dのみ";
			if (pickDimension == SceneViewPickDimension::Type2D) {
				dimensionLabel = "2Dのみ";
			} else if (pickDimension == SceneViewPickDimension::Both) {
				dimensionLabel = "2D・3D";
			} else {
				dimensionLabel = "3Dのみ";
			}
			const std::string tooltip = std::string("選択できるエンティティ次元\n現在の状態: ") + dimensionLabel;
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
		if (DrawIconButton(
				"##ManipulatorNone", GetTextureID(icons_.noneKey), mode == SceneViewManipulatorMode::None, buttonSize_)) {

			mode = SceneViewManipulatorMode::None;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("マニュピレーター表示なし H");
		}
	}
	// EntityとSubMeshの選択を切り替える
	{
		EditorSelectionKind& kind = context.editorState->selectKind;
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
								  (kind == EditorSelectionKind::Entity ? "エンティティ単位" : "サブメッシュ単位 E");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
	}
	DrawToolSeparator(buttonSize_);
	// 移動・回転・拡縮を選ぶ
	{
		if (DrawIconButton("##ManipulatorTranslate", GetTextureID(icons_.translateKey),
				mode == SceneViewManipulatorMode::Translate, buttonSize_)) {

			mode = SceneViewManipulatorMode::Translate;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("座標編集 T");
		}
		if (DrawIconButton(
				"##ManipulatorRotate", GetTextureID(icons_.rotateKey), mode == SceneViewManipulatorMode::Rotate, buttonSize_)) {

			mode = SceneViewManipulatorMode::Rotate;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("回転編集 R");
		}
		if (DrawIconButton(
				"##ManipulatorScale", GetTextureID(icons_.scaleKey), mode == SceneViewManipulatorMode::Scale, buttonSize_)) {

			mode = SceneViewManipulatorMode::Scale;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("拡縮編集 S");
		}
	}
	DrawToolSeparator(buttonSize_);
	// スナップ操作を切り替える
	{
		if (DrawIconButton("##EnableSnapEntity", GetTextureID(icons_.snapEditEntityKey),
				context.editorState->enableSnapEditEntity, buttonSize_)) {

			context.editorState->enableSnapEditEntity = !context.editorState->enableSnapEditEntity;
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip =
				std::string("エンティティスナップ操作の有効/無効切り替え\n左クリックで切替 右クリックで設定\n現在の状態: ") +
				(context.editorState->enableSnapEditEntity ? "有効" : "無効 G");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
		// 右クリックでスナップ単位を編集する
		ImGui::OpenPopupOnItemClick("SnapSettingsPopup", ImGuiPopupFlags_MouseButtonRight);
		DrawSnapSettingsPopup(context);
	}
	// 複数選択のピボットを切り替える
	{
		const std::string& pivotIcon =
			context.editorState->gizmoPivotAtCenter ? icons_.gizmoCenterPivotKey : icons_.eachEntityOriginKey;
		if (DrawIconButton("##GizmoPivotMode", GetTextureID(pivotIcon), true, buttonSize_)) {

			context.editorState->gizmoPivotAtCenter = !context.editorState->gizmoPivotAtCenter;
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip = std::string("複数選択ギズモのピボット\n現在: ") +
								  (context.editorState->gizmoPivotAtCenter ? "選択中心" : "各エンティティ原点");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
	}
}

void Engine::ViewportToolbar::DrawSnapSettingsPopup(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	if (!ImGui::BeginPopup("SnapSettingsPopup")) {
		return;
	}

	EntitySnapSettings& settings = context.editorState->snapSettings;

	// スナップ単位と絶対座標を同じ行で編集する
	auto drawSnapRow = [](const char* id, const char* label, GridSnapAxis& axis, float dragSpeed) {
		ImGui::PushID(id);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine();

		ImGui::SetNextItemWidth(150.0f);
		ImGui::DragFloat("##size", &axis.size, dragSpeed, 0.0001f, 1000.0f, "%.3f");
		if (axis.size < 0.0001f) {
			axis.size = 0.0001f;
		}
		ImGui::SameLine();
		ImGui::Checkbox("##absolute", &axis.absolute);
		ImGui::PopID();
	};

	ImGui::TextDisabled("スナップ単位 右のチェックで最寄りグリッドへ強制");

	ImGui::SeparatorText("2D");
	drawSnapRow("t2d", "移動", settings.translate2D, 0.01f);
	drawSnapRow("r2d", "回転", settings.rotate2D, 0.1f);
	drawSnapRow("s2d", "拡縮", settings.scale2D, 0.01f);

	ImGui::SeparatorText("3D");
	drawSnapRow("t3d", "移動", settings.translate3D, 0.01f);
	drawSnapRow("r3d", "回転", settings.rotate3D, 0.1f);
	drawSnapRow("s3d", "拡縮", settings.scale3D, 0.01f);

	ImGui::Separator();
	ImGui::Checkbox("スナップグリッドを表示", &settings.drawSnapGrid);

	ImGui::EndPopup();
}

void Engine::ViewportToolbar::DrawGridSection(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	if (DrawIconButton("##SceneViewDefaultGrid", GetTextureID(icons_.drawGridKey),
			context.editorState->drawSceneViewDefaultGrid, buttonSize_)) {

		context.editorState->drawSceneViewDefaultGrid = !context.editorState->drawSceneViewDefaultGrid;
	}
	if (ImGui::IsItemHovered()) {

		std::string tooltip = std::string("SceneViewグリッド表示\n現在の状態: ") +
							  (context.editorState->drawSceneViewDefaultGrid ? "有効" : "無効");
		ImGui::SetTooltip("%s", tooltip.c_str());
	}
}
