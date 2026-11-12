#include "ViewportToolbar.h"
#include "ViewportCameraSelection.h"
#include "ViewportOrientationGizmo.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Common/EditorIconButton.h>

// c++
#include <algorithm>
#include <array>

namespace {

	enum class ToolAction { None, Translate, Rotate, Scale, Pick, Selection, Dimension, Pivot, Snap, Grid, Manual, Camera, Prefab };
	struct ToolButton {

		ToolAction action;
		const std::string* icon;
		const char* tooltip;
		bool selected;
		int group;
	};

	// 三軸のアイコンで軸表示を切り替える
	void DrawOrientationToggle(Engine::EditorState& state, const ImVec2& size) {

		if (Engine::EditorIconButton::Draw("SceneOrientationToggle", "", "軸表示", state.drawSceneViewOrientation, size)) {
			state.drawSceneViewOrientation = !state.drawSceneViewOrientation;
			state.sceneOrientationHovered = state.sceneOrientationActive = false;
			state.sceneOrientationMin = state.sceneOrientationMax = {};
		}
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f + size.y * 0.1f);
		const float scale = size.y / 24.0f;
		const ImVec2 ends[]{ImVec2(center.x + 7 * scale, center.y + 3 * scale),
			ImVec2(center.x, center.y - 8 * scale), ImVec2(center.x - 7 * scale, center.y + 3 * scale)};
		for (int index = 0; index < 3; ++index) {
			const ImU32 color = Engine::ViewportOrientationGizmo::GetAxisColor(index);
			ImGui::GetWindowDrawList()->AddLine(center, ends[index], color, 2 * scale);
			ImGui::GetWindowDrawList()->AddCircleFilled(ends[index], 2 * scale, color);
		}
	}
}

Engine::ViewportToolbar::ViewportToolbar(TextureUploadService& textureUploadService)
	: textureUploadService_(textureUploadService) {

	RequestIcons();
}

void Engine::ViewportToolbar::Draw(const EditorPanelContext& context) {

	if (!context.editorState) return;
	auto& state = *context.editorState;
	std::array<ToolButton, 13> buttons{};
	size_t count = 0;
	auto add = [&](ToolAction action, const std::string& icon, const char* tip, bool selected, int group) {
		buttons[count++] = {action, &icon, tip, selected, group};
	};
	if (context.editorContext && context.editorContext->isPrefabEditing)
		add(ToolAction::Prefab, icons_.prefabExitKey, "In-Context編集の切り替え", context.editorContext->isPrefabInContext, -1);
	add(ToolAction::None, icons_.noneKey, "マニピュレーター表示なし H", state.sceneViewManipulatorMode == SceneViewManipulatorMode::None, 0);
	add(ToolAction::Translate, icons_.translateKey, "座標編集 T", state.sceneViewManipulatorMode == SceneViewManipulatorMode::Translate, 0);
	add(ToolAction::Rotate, icons_.rotateKey, "回転編集 R", state.sceneViewManipulatorMode == SceneViewManipulatorMode::Rotate, 0);
	add(ToolAction::Scale, icons_.scaleKey, "拡縮編集 S", state.sceneViewManipulatorMode == SceneViewManipulatorMode::Scale, 0);
	add(ToolAction::Pick, icons_.enablePickKey, "シーンオブジェクト選択の有効・無効", state.enableScenePick, 1);
	add(ToolAction::Selection, state.selectKind == EditorSelectionKind::Entity ? icons_.entitySelectKey : icons_.subMeshSelectKey,
		"エンティティ・サブメッシュ選択 E", state.selectKind == EditorSelectionKind::MeshSubMesh, 1);
	add(ToolAction::Dimension, state.sceneViewPickDimension == SceneViewPickDimension::Type2D ? icons_.selection2DKey :
		state.sceneViewPickDimension == SceneViewPickDimension::Type3D ? icons_.selection3DKey : icons_.selection2DAnd3DKey,
		"選択対象の次元 3D・2D・両方", state.sceneViewPickDimension != SceneViewPickDimension::Type3D, 1);
	add(ToolAction::Pivot, state.gizmoPivotAtCenter ? icons_.gizmoCenterPivotKey : icons_.eachEntityOriginKey,
		"ピボット 選択中心・各エンティティ原点", state.gizmoPivotAtCenter, 2);
	add(ToolAction::Snap, icons_.snapEditEntityKey, "スナップ G / 右クリックで設定", state.enableSnapEditEntity, 2);
	add(ToolAction::Grid, icons_.drawGridKey, "SceneViewグリッド表示", state.drawSceneViewDefaultGrid, 2);
	add(ToolAction::Manual, icons_.debugCameraKey, "マニュアルカメラ操作", state.sceneViewCamera.mode == SceneViewCameraMode::DebugManual, 3);
	add(ToolAction::Camera, icons_.entityCameraKey, "エンティティカメラによる制御",
		state.sceneViewCamera.mode == SceneViewCameraMode::SelectedEntityCamera, 3);

	bool cameraPopup = false, snapPopup = false;
	auto apply = [&](ToolAction action) {
		switch (action) {
		case ToolAction::None: state.sceneViewManipulatorMode = SceneViewManipulatorMode::None; break;
		case ToolAction::Translate: state.sceneViewManipulatorMode = SceneViewManipulatorMode::Translate; break;
		case ToolAction::Rotate: state.sceneViewManipulatorMode = SceneViewManipulatorMode::Rotate; break;
		case ToolAction::Scale: state.sceneViewManipulatorMode = SceneViewManipulatorMode::Scale; break;
		case ToolAction::Pick: state.enableScenePick = !state.enableScenePick; break;
		case ToolAction::Selection:
			state.selectKind = state.selectKind == EditorSelectionKind::Entity ? EditorSelectionKind::MeshSubMesh : EditorSelectionKind::Entity;
			break;
		case ToolAction::Dimension:
			state.sceneViewPickDimension = state.sceneViewPickDimension == SceneViewPickDimension::Type3D ? SceneViewPickDimension::Type2D :
				state.sceneViewPickDimension == SceneViewPickDimension::Type2D ? SceneViewPickDimension::Both : SceneViewPickDimension::Type3D;
			break;
		case ToolAction::Pivot: state.gizmoPivotAtCenter = !state.gizmoPivotAtCenter; break;
		case ToolAction::Snap: state.enableSnapEditEntity = !state.enableSnapEditEntity; break;
		case ToolAction::Grid: state.drawSceneViewDefaultGrid = !state.drawSceneViewDefaultGrid; break;
		case ToolAction::Manual:
			state.sceneViewCamera.mode = SceneViewCameraMode::DebugManual;
			state.sceneViewCamera.ClearAssignedCameras();
			break;
		case ToolAction::Camera:
			state.sceneViewCamera.mode = SceneViewCameraMode::SelectedEntityCamera;
			cameraPopup = true;
			break;
		case ToolAction::Prefab: if (context.host) context.host->RequestTogglePrefabInContext(); break;
		}
	};
	const float scale = ImGui::GetFontSize() / 20.0f;
	const ImVec2 size(36.0f * scale, 24.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.025f, 0.025f, 0.025f, 1));
	ImGui::BeginChild("SceneToolbar", ImVec2(0, size.y), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float right = origin.x + ImGui::GetContentRegionAvail().x;
	const float toolsRight = right - size.x;
	size_t index = 0;
	for (; index < count; ++index) {
		const auto& item = buttons[index];
		if (ImGui::GetCursorScreenPos().x + size.x * (index + 1 < count ? 2 : 1) > toolsRight) break;
		ImGui::PushID(static_cast<int>(item.action));
		if (EditorIconButton::DrawImage("Tool", GetTextureID(*item.icon), item.tooltip, item.selected, size)) apply(item.action);
		if (item.action == ToolAction::Snap && ImGui::IsItemClicked(ImGuiMouseButton_Right)) snapPopup = true;
		ImGui::PopID();
		ImGui::SameLine();
	}
	if (index < count && ImGui::GetCursorScreenPos().x + size.x <= toolsRight &&
		EditorIconButton::Draw("ToolOverflowButton", "...", "その他の操作", false, size)) ImGui::OpenPopup("ToolOverflow");
	ImGui::SetCursorScreenPos(ImVec2((std::max)(origin.x, toolsRight), origin.y));
	DrawOrientationToggle(state, ImVec2((std::min)(size.x, right - origin.x), size.y));
	ImGui::PopStyleVar();
	if (ImGui::BeginPopup("ToolOverflow")) {
		for (size_t hidden = index; hidden < count; ++hidden) {
			const auto& item = buttons[hidden];
			if (hidden > index && item.group != buttons[hidden - 1].group) ImGui::Separator();
			if (ImGui::MenuItem(item.tooltip, nullptr, item.selected)) apply(item.action);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("スナップ設定")) snapPopup = true;
		ImGui::EndPopup();
	}
	if (cameraPopup) ImGui::OpenPopup("##SceneEntityCameraPopup");
	if (snapPopup) ImGui::OpenPopup("SnapSettingsPopup");
	DrawSnapSettingsPopup(context);
	ViewportCameraSelection::DrawPopup(context);
	ImGui::EndChild();
	ImGui::PopStyleColor();
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
