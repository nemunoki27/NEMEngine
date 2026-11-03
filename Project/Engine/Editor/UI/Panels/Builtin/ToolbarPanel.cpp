#include "ToolbarPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptBuildService.h>

//============================================================================
//	ToolbarPanel classMethods
//============================================================================
Engine::ToolbarPanel::ToolbarPanel(TextureUploadService& textureUploadService) :
	textureUploadService_(&textureUploadService) {

	icons_.playKey = "editor:toolbar:play";
	icons_.stopKey = "editor:toolbar:stop";
	icons_.pauseKey = "editor:toolbar:pause";
	icons_.frameStepKey = "editor:toolbar:frameStep";
}

void Engine::ToolbarPanel::Draw(const EditorPanelContext& context) {

	// 以前のレイアウトからツールバーのドッキング設定を取り除く
	if (ImGui::FindWindowSettingsByID(ImHashStr("Toolbar"))) {
		ImGui::DockBuilderDockWindow("Toolbar", 0);
		ImGui::ClearWindowSettings("Toolbar");
	}

	// ツールバーパネルの表示状態を確認
	if (!context.layoutState->showToolbar) {
		return;
	}
	RequestIcons();

	// メニューバーの下に固定して表示領域を確保する
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;
	const float fontScale = 0.85f;
	const ImVec2 playbackButtonSize(54.0f, 20.0f);
	const float contentHeight = ImMax(ImGui::GetFontSize() * fontScale, playbackButtonSize.y);
	const float height = contentHeight + ImGui::GetStyle().WindowPadding.y * 2.0f;
	ImGui::SetNextWindowDockID(0, ImGuiCond_Always);

	if (!ImGui::BeginViewportSideBar("Toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags)) {
		ImGui::End();
		return;
	}

	ImGui::SetWindowFontScale(fontScale);

	//============================================================================
	//	現在のシーン名、モード、Undo/Redoの状態を表示
	//============================================================================
	const float toolbarY = (ImGui::GetWindowHeight() - contentHeight) * 0.5f;
	const float textY = toolbarY + (contentHeight - ImGui::GetFontSize()) * 0.5f;
	const float toolbarStartX = ImGui::GetCursorPosX();
	const float toolbarWidth = ImGui::GetContentRegionAvail().x;
	ImGui::SetCursorPosY(textY);

	if (context.editorContext && context.editorContext->activeSceneHeader) {

		// 未保存の変更があれば末尾に*を付けて保存状態を分かるようにする
		const char* dirtyMark = context.editorContext->activeSceneDirty ? "*" : "";
		ImGui::Text("現在のシーン : %s%s", context.editorContext->activeSceneHeader->name.c_str(), dirtyMark);
	} else {

		ImGui::Text("現在のシーン : <None>");
	}

	ImGui::SameLine(0.0f, 16.0f);
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::Text("モード : %s", context.IsPlayPaused() ? "ポーズ中" : (context.IsPlaying() ? "再生中" : "編集中"));

	// Undo/Redoの数チェック
	ImGui::SameLine(0.0f, 16.0f);
	ImGui::Text("Undo:%d / Redo: %d", context.editorState->commandHistory.GetUndoCount(),
		context.editorState->commandHistory.GetRedoCount());

	//============================================================================
	//	シーンのプレイ/ストップ切り替え
	//============================================================================
	const bool isPlaying = context.IsPlaying();
	const bool isPaused = context.IsPlayPaused();
	const float playbackButtonSpacing = isPlaying ? 6.0f : 0.0f;
	const float playbackGroupWidth = playbackButtonSize.x + (isPlaying ? playbackButtonSpacing + playbackButtonSize.x : 0.0f);

	const float playbackY = toolbarY + (contentHeight - playbackButtonSize.y) * 0.5f;
	ImGui::SetCursorPos(ImVec2(toolbarStartX + (toolbarWidth - playbackGroupWidth) * 0.5f, playbackY));

	if (!isPlaying) {

		if (DrawIconButton("##ToolbarPlay", GetTextureID(icons_.playKey), playbackButtonSize)) {
			context.host->RequestPlayToggle();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("再生");
		}

		// Play押下後はGameScriptsのbuild/reload完了までPlayモードに入らないので、その間は進行中であることを示す
		const ManagedScriptBuildService::PlayBuildResult playBuildResult =
			context.editorContext && context.editorContext->scriptBuildService ?
			context.editorContext->scriptBuildService->PollPlayBuild() :
			ManagedScriptBuildService::PlayBuildResult::Succeeded;
		if (playBuildResult == ManagedScriptBuildService::PlayBuildResult::Pending) {

			ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetCursorPosY(textY);
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "ビルド中...");
		} else if (playBuildResult == ManagedScriptBuildService::PlayBuildResult::Failed) {

			ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetCursorPosY(textY);
			ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "ビルドに失敗しました");
		}
	} else {

		const std::string& primaryIcon = isPaused ? icons_.playKey : icons_.stopKey;
		if (DrawIconButton("##ToolbarPlayPrimary", GetTextureID(primaryIcon), playbackButtonSize)) {
			if (isPaused) {
				context.host->RequestPlayResume();
			} else {
				context.host->RequestPlayToggle();
			}
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(isPaused ? "再生" : "停止");
		}

		ImGui::SameLine(0.0f, playbackButtonSpacing);

		const std::string& secondaryIcon = isPaused ? icons_.frameStepKey : icons_.pauseKey;
		if (DrawIconButton("##ToolbarPlaySecondary", GetTextureID(secondaryIcon), playbackButtonSize)) {
			if (isPaused) {
				context.host->RequestPlayFrameStep();
			} else {
				context.host->RequestPlayPause();
			}
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(isPaused ? "1フレーム進める" : "ポーズ");
		}
	}

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
}

void Engine::ToolbarPanel::RequestIcons() {

	if (iconsRequested_ || !textureUploadService_) {
		return;
	}

	textureUploadService_->RequestTextureFile(icons_.playKey,
		EditorTextureHelper::MakeEditorTexturePath("Toolbar", "gamePlayIcon.png"));
	textureUploadService_->RequestTextureFile(icons_.stopKey,
		EditorTextureHelper::MakeEditorTexturePath("Toolbar", "gameStopIcon.png"));
	textureUploadService_->RequestTextureFile(icons_.pauseKey,
		EditorTextureHelper::MakeEditorTexturePath("Toolbar", "gamePauseIcon.png"));
	textureUploadService_->RequestTextureFile(icons_.frameStepKey,
		EditorTextureHelper::MakeEditorTexturePath("Toolbar", "gameFrameNextIcon.png"));
	iconsRequested_ = true;
}

ImTextureID Engine::ToolbarPanel::GetTextureID(const std::string& key) const {

	if (!textureUploadService_) {
		return ImTextureID{};
	}
	return EditorTextureHelper::GetImTextureID(*textureUploadService_, key);
}

bool Engine::ToolbarPanel::DrawIconButton(const char* id, ImTextureID textureID, const ImVec2& size) const {

	const bool result = ImGui::InvisibleButton(id, size);

	const bool active = ImGui::IsItemActive();
	const bool hovered = ImGui::IsItemHovered();
	const ImVec4 buttonColor = active ? ImVec4(0.02f, 0.02f, 0.02f, 1.00f) :
		(hovered ? ImVec4(0.08f, 0.08f, 0.08f, 0.98f) : ImVec4(0.04f, 0.04f, 0.04f, 0.95f));

	const ImVec2 rectMin = ImGui::GetItemRectMin();
	const ImVec2 rectMax = ImGui::GetItemRectMax();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(buttonColor), 4.0f);

	constexpr float kIconPadding = 1.0f;
	const float shortSide = size.x < size.y ? size.x : size.y;
	const float iconSide = shortSide - kIconPadding * 2.0f;
	if (0.0f < iconSide && textureID != ImTextureID{}) {

		const ImVec2 iconMin(
			rectMin.x + (size.x - iconSide) * 0.5f,
			rectMin.y + (size.y - iconSide) * 0.5f);
		const ImVec2 iconMax(iconMin.x + iconSide, iconMin.y + iconSide);
		drawList->AddImage(textureID, iconMin, iconMax);
	}
	return result;
}
