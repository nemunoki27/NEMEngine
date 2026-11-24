#include "ShaderGraphEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/Utility/EditorSourceNavigation.h>

// c++
#include <algorithm>
#include <utility>

#include <imgui.h>
#include <imgui_node_editor.h>


namespace {

	namespace ed = ax::NodeEditor;

	bool DrawVerticalTab(const char* label, float width, bool selected) {

		const ImVec2 text = ImGui::CalcTextSize(label);
		const float padding = ImGui::GetStyle().FramePadding.x;
		const bool pressed = ImGui::InvisibleButton(label, ImVec2(width, text.x + padding * 2.0f), ImGuiButtonFlags_EnableNav);
		if (!ImGui::IsItemVisible()) {
			return pressed;
		}
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 start = ImGui::GetItemRectMin();
		const ImVec2 end = ImGui::GetItemRectMax();
		const ImGuiCol color = selected ? ImGuiCol_TabSelected : ImGui::IsItemHovered() ? ImGuiCol_TabHovered : ImGuiCol_Tab;
		draw->AddRectFilled(start, end, ImGui::GetColorU32(color));
		if (selected) {
			draw->AddRectFilled(start, ImVec2(start.x + 2.0f, end.y), ImGui::GetColorU32(ImGuiCol_TabSelectedOverline));
		}

		// ラベルの頂点だけを右回りに90度回転する
		const int first = draw->VtxBuffer.Size;
		ImGui::GetFont()->RenderText(draw, ImGui::GetFontSize(), ImVec2(), ImGui::GetColorU32(ImGuiCol_Text),
			ImVec4(-width, -width, text.x + width, text.y + width), label, nullptr);
		const ImVec2 origin(start.x + (width + text.y) * 0.5f, start.y + padding);
		for (int i = first; i < draw->VtxBuffer.Size; ++i) {
			ImVec2& point = draw->VtxBuffer[i].pos;
			point = ImVec2(origin.x - point.y, origin.y + point.x);
		}
		return pressed;
	}


}

//============================================================================
//	ShaderGraphEditorTool classMethods
//============================================================================

Engine::ShaderGraphEditorTool::ShaderGraphEditorTool()
	: nodeTransfer_(editSession_), nodeDrawer_(editSession_, appearanceEditor_.GetSettings(), nodePreviews_),
	  groupEditor_(editSession_, appearanceEditor_.GetSettings()),
	  canvasMenu_(editSession_, canvas_, groupEditor_, nodeTransfer_),
	  canvasView_(editSession_, canvas_, appearanceEditor_, nodePreviews_, nodeDrawer_, groupEditor_, canvasMenu_),
	previewController_(editSession_), settingsEditor_(editSession_) {

	// ノード画像の描画状態を初期化する
	nodePreviews_.Init();
}

Engine::ShaderGraphEditorTool::~ShaderGraphEditorTool() {

	// 描画資源を回収へ渡して編集画面を解除する
	nodePreviews_.ClearNodePreviews();
	ResetNodeEditor();
}

void Engine::ShaderGraphEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ShaderGraphEditorTool::OpenAsset(AssetID assetID) {

	pendingAsset_ = assetID;
	openWindow_ = true;
}

void Engine::ShaderGraphEditorTool::DrawEditorTool(const EditorToolContext& context) {

	// 外部から指定されたグラフへ切替を要求する
	commandPanelFocused_ = false;
	if (pendingAsset_) {
		RequestGraphSwitch(context, pendingAsset_);
		pendingAsset_ = {};
	}
	if (openWindow_) {
		DrawWindow(context);
	}
	DrawUnsavedPrompt(context);
}

void Engine::ShaderGraphEditorTool::DrawWindow(const EditorToolContext& context) {

	const bool wasOpen = openWindow_;
	const bool visible = Engine::EditorWindowPresentation::Begin(MakeWindowName("シェーダーグラフ").c_str(), &openWindow_, ImGuiWindowFlags_MenuBar);
	commandPanelFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (!visible) {

		ImGui::End();
		if (wasOpen && !openWindow_) {
			HandleWindowClose();
		}
		return;
	}

	// 操作欄と設定とキャンバスを順に表示する
	editSession_.RefreshReferences(context);
	toolbar_.Draw(context, editSession_, *this);
	ImGui::Separator();

	DrawWorkspace(context);
	if (editSession_.IsLoaded() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {

		CommitGraphHistory();
	}
	ImGui::End();
	if (wasOpen && !openWindow_) {
		HandleWindowClose();
	}
}

void Engine::ShaderGraphEditorTool::HandleWindowClose() {

	// 未保存の編集を確認するまでウィンドウを保持する
	if (editSession_.IsDirty()) {
		openWindow_ = true;
		requestWindowClose_ = true;
		requestUnsavedPrompt_ = true;
	}
}

bool Engine::ShaderGraphEditorTool::HasPendingEdits() const {

	return editSession_.IsDirty();
}

void Engine::ShaderGraphEditorTool::RequestResolvePendingEdits() {

	if (!editSession_.IsDirty()) {
		return;
	}
	openWindow_ = true;
	resolvingClose_ = true;
	requestAssetSwitch_ = false;
	requestGraphCreate_ = false;
	requestWindowClose_ = true;
	requestUnsavedPrompt_ = true;
	pendingEditCloseResult_ = EditorToolCloseResult::None;
}

Engine::EditorToolCloseResult Engine::ShaderGraphEditorTool::ConsumePendingEditCloseResult() {

	const EditorToolCloseResult result = pendingEditCloseResult_;
	pendingEditCloseResult_ = EditorToolCloseResult::None;
	return result;
}

void Engine::ShaderGraphEditorTool::DrawUnsavedPrompt(const EditorToolContext& context) {

	constexpr const char* kPopup = "ShaderGraphの未保存編集";
	if (requestUnsavedPrompt_) {
		ImGui::OpenPopup(kPopup);
		requestUnsavedPrompt_ = false;
	}
	if (!MyGUI::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextWrapped("%s", "シェーダーグラフに未保存の変更があります。");
	ImGui::Separator();
	if (ImGui::Button("保存", ImVec2(96.0f, 0.0f))) {
		if (SaveGraph(context)) {
			pendingEditCloseResult_ = EditorToolCloseResult::Accepted;
			ApplyPendingTransition(context);
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("破棄", ImVec2(96.0f, 0.0f))) {
		pendingEditCloseResult_ = EditorToolCloseResult::Accepted;
		ApplyPendingTransition(context);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(96.0f, 0.0f))) {
		requestAssetSwitch_ = false;
		requestGraphCreate_ = false;
		requestWindowClose_ = false;
		requestedAsset_ = {};
		resolvingClose_ = false;
		openWindow_ = true;
		pendingEditCloseResult_ = EditorToolCloseResult::Cancelled;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Engine::ShaderGraphEditorTool::DrawGraphSettings() {

	if (settingsEditor_.Draw()) {
		ResetNodeEditor();
		canvas_.RequestPositionRestore();
	}
}

void Engine::ShaderGraphEditorTool::DrawGraph(const EditorToolContext& context) {

	if (!editSession_.IsLoaded()) {
		return;
	}
	// Canvas操作を保存と履歴の処理へ渡す
	for (ShaderGraphCanvasCommand command : canvasView_.Draw(context, commandPanelFocused_)) {
		switch (command) {
		case ShaderGraphCanvasCommand::Copy:
			CopySelection();
			break;
		case ShaderGraphCanvasCommand::Paste:
			PasteSelection();
			break;
		case ShaderGraphCanvasCommand::Duplicate:
			CopySelection();
			PasteSelection();
			break;
		case ShaderGraphCanvasCommand::Save:
			SaveAndCompile(context);
			break;
		case ShaderGraphCanvasCommand::Undo:
			UndoGraph();
			break;
		case ShaderGraphCanvasCommand::Redo:
			RedoGraph();
			break;
		}
	}
	ed::SetCurrentEditor(nullptr);
}

void Engine::ShaderGraphEditorTool::ImportGraphSettings(const EditorToolContext& context, AssetID source) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!editSession_.IsLoaded() || !database) {
		editSession_.GetStatusMessage() = "別のMaterialまたはShaderGraphを指定してください";
		return;
	}
	ShaderGraphAsset imported;
	if (!ShaderGraphAssetAuthoring::Import(
			*database, editSession_.GetDraft(), editSession_.GetAssetID(), source, imported, editSession_.GetStatusMessage())) {
		return;
	}

	CaptureNodePositions();
	editSession_.CaptureHistory();

	nodePreviews_.ClearNodePreviews();
	editSession_.Import(std::move(imported));
	parameterEditor_.ResetSelection();
	groupEditor_.Reset();
	canvasMenu_.Reset();
	ResetNodeEditor();
	canvas_.RequestPositionRestore();
	editSession_.GetStatusMessage() = "設定をインポートしました。保存してください";
}

bool Engine::ShaderGraphEditorTool::CreateGraph(const EditorToolContext& context) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database) {
		editSession_.GetStatusMessage() = "作成先を設定してください";
		return false;
	}
	const AssetID assetID =
		ShaderGraphAssetAuthoring::Create(*database, toolbar_.GetCreationRequest(), editSession_.GetStatusMessage());
	return assetID && LoadGraph(context, assetID);
}

void Engine::ShaderGraphEditorTool::CaptureNodePositions() {

	// Canvasの位置を保存用Graphへ反映する
	canvas_.CapturePositions(editSession_.GetDraft());
}

void Engine::ShaderGraphEditorTool::ResetNodeEditor() {

	// Graph切替前のCanvasと入力状態を破棄する
	canvas_.Reset();
	nodeDrawer_.Reset();
	groupEditor_.Reset();
}

std::vector<Engine::UUID> Engine::ShaderGraphEditorTool::GetSelectedGraphNodes() const {

	return canvas_.GetSelectedNodes(editSession_.GetDraft());
}

void Engine::ShaderGraphEditorTool::CopySelection() {

	// 選択範囲のNodeと内部接続をコピーする
	nodeTransfer_.CopySelection(GetSelectedGraphNodes());
}

void Engine::ShaderGraphEditorTool::PasteSelection() {

	// 貼り付け位置を確定して履歴へまとめる
	if (nodeTransfer_.PasteSelection()) {
		CommitGraphHistory();
	}
}

void Engine::ShaderGraphEditorTool::UndoGraph() {

	CommitGraphHistory();
	if (!editSession_.Undo()) {
		return;
	}
	nodePreviews_.InvalidateNodePreviews();

	canvas_.RequestPositionRestore();
	groupEditor_.Reset();
	canvasMenu_.Reset();
	parameterEditor_.ResetSelection();
	ResetNodeEditor();
	editSession_.GetStatusMessage() = "編集を元に戻しました";
}

void Engine::ShaderGraphEditorTool::RedoGraph() {

	if (!editSession_.Redo()) {
		return;
	}
	nodePreviews_.InvalidateNodePreviews();

	canvas_.RequestPositionRestore();
	groupEditor_.Reset();
	canvasMenu_.Reset();
	parameterEditor_.ResetSelection();
	ResetNodeEditor();
	editSession_.GetStatusMessage() = "編集をやり直しました";
}

void Engine::ShaderGraphEditorTool::CommitGraphHistory() {

	CaptureNodePositions();
	editSession_.Commit();
}

bool Engine::ShaderGraphEditorTool::LoadGraph(const EditorToolContext& context, AssetID assetID) {

	const bool loaded = editSession_.Load(context, assetID);
	if (!loaded) {
		if (!context.toolContext.assetDatabase || !assetID) {
			nodePreviews_.ClearNodePreviews();
			ResetNodeEditor();
		}
		return false;
	}
	nodePreviews_.ClearNodePreviews();
	parameterEditor_.ResetSelection();
	ResetNodeEditor();
	canvas_.RequestPositionRestore();
	return true;
}

void Engine::ShaderGraphEditorTool::RequestGraphSwitch(const EditorToolContext& context, AssetID assetID) {

	if (assetID == editSession_.GetAssetID()) {
		return;
	}
	if (!editSession_.IsDirty()) {

		LoadGraph(context, assetID);
		return;
	}
	requestedAsset_ = assetID;
	requestAssetSwitch_ = true;
	requestGraphCreate_ = false;
	requestWindowClose_ = false;
	requestUnsavedPrompt_ = true;
}

void Engine::ShaderGraphEditorTool::ApplyPendingTransition(const EditorToolContext& context) {

	if (requestAssetSwitch_) {
		const AssetID assetID = requestedAsset_;
		requestAssetSwitch_ = false;
		requestedAsset_ = {};
		LoadGraph(context, assetID);
	}
	if (requestGraphCreate_) {
		requestGraphCreate_ = false;
		CreateGraph(context);
	}
	if (requestWindowClose_) {
		if (editSession_.IsDirty()) {
			editSession_.Load(context, editSession_.GetAssetID());
		}
		requestWindowClose_ = false;
		openWindow_ = resolvingClose_;
		resolvingClose_ = false;
	}
}

bool Engine::ShaderGraphEditorTool::SaveAndCompile(const EditorToolContext& context) {

	const auto graphPath = editSession_.ResolveCompilePath(context);
	if (graphPath.empty()) {
		return false;
	}
	CaptureNodePositions();
	return editSession_.SaveAndCompile(context, graphPath);
}

void Engine::ShaderGraphEditorTool::DrawWorkspace(const EditorToolContext& context) {

	const bool loaded = editSession_.IsLoaded();
	struct TabEntry {
		SettingsTab tab;
		const char* label;
		bool visible;
	};
	const TabEntry entries[] = {
		{SettingsTab::Appearance, "見た目設定", EditorSourceNavigation::IsEngineSourceProject()},
		{SettingsTab::Graph, "グラフ設定", loaded},
		{SettingsTab::Parameters, "パラメータ", loaded}, {SettingsTab::Keywords, "キーワード", loaded},
		{SettingsTab::Selection, "選択ノード", loaded}, {SettingsTab::Diagnostics, "診断", loaded},
	};

	float tabsHeight = 0.0f;
	const ImGuiStyle& style = ImGui::GetStyle();
	for (const TabEntry& entry : entries) {
		if (entry.visible) {
			tabsHeight += ImGui::CalcTextSize(entry.label).x + style.FramePadding.x * 2.0f + style.ItemSpacing.y;
		} else if (settingsTab_ == entry.tab) {
			settingsTab_ = SettingsTab::None;
		}
	}
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const float tabWidth = ImGui::GetFrameHeight();
	const float railWidth = tabsHeight > 0.0f ? tabWidth +
		(tabsHeight - style.ItemSpacing.y > available.y ? style.ScrollbarSize : 0.0f) : 0.0f;
	const float workspace = (std::max)(1.0f, available.x - railWidth);
	const float grip = style.GrabMinSize * 0.5f;
	const float maxWidth = (std::max)(1.0f, workspace * 0.8f - grip);
	const float minWidth = (std::min)(180.0f, maxWidth);
	const float width = std::clamp(settingsWidth_, minWidth, maxWidth);
	const bool open = settingsTab_ != SettingsTab::None;
	const float canvasWidth = (std::max)(1.0f, workspace - (open ? width + grip : 0.0f));

	if (ImGui::BeginChild("ShaderGraphCanvas", ImVec2(canvasWidth, 0.0f), ImGuiChildFlags_Borders)) {
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		DrawGraph(context);
		if (loaded) previewController_.Draw(context, origin, size);
	}
	ImGui::EndChild();
	if (open) {
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::InvisibleButton("##SettingsWidth", ImVec2(grip, (std::max)(1.0f, available.y)));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}
		if (ImGui::IsItemActive()) {
			settingsWidth_ = std::clamp(width - ImGui::GetIO().MouseDelta.x, minWidth, maxWidth);
		}
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::PushID(static_cast<int>(settingsTab_));
		if (ImGui::BeginChild("ShaderGraphParameters", ImVec2(width, 0.0f), ImGuiChildFlags_Borders)) {
			switch (settingsTab_) {
			case SettingsTab::Appearance: DrawAppearancePanel(); break;
			case SettingsTab::Graph: DrawGraphSettings(); break;
			case SettingsTab::Parameters: parameterEditor_.Draw(context, editSession_); break;
			case SettingsTab::Keywords: keywordEditor_.Draw(editSession_); break;
			case SettingsTab::Selection: DrawSelectedNodeEditor(context); break;
			case SettingsTab::Diagnostics: canvasView_.DrawDiagnostics(); break;
			default: break;
			}
		}
		ImGui::EndChild();
		ImGui::PopID();
	}
	if (railWidth <= 0.0f) {
		return;
	}
	ImGui::SameLine(0.0f, 0.0f);
	if (ImGui::BeginChild("ShaderGraphTabs", ImVec2(railWidth, 0.0f))) {
		for (const TabEntry& entry : entries) {
			if (entry.visible && DrawVerticalTab(entry.label, tabWidth, entry.tab == settingsTab_)) {
				settingsTab_ = entry.tab == settingsTab_ ? SettingsTab::None : entry.tab;
			}
		}
	}
	ImGui::EndChild();
}

void Engine::ShaderGraphEditorTool::DrawSelectedNodeEditor(const EditorToolContext& context) {

	if (!canvas_.GetEditor()) {
		return;
	}
	// Node Editorの選択をInspectorへ渡す
	ed::SetCurrentEditor(canvas_.GetEditor());
	const auto selected = GetSelectedGraphNodes();
	ed::SetCurrentEditor(nullptr);
	nodeInspector_.Draw(context, editSession_, selected);
}

void Engine::ShaderGraphEditorTool::DrawAppearancePanel() {

	// 解像度の変更が確定したときだけpreviewを作り直す
	if (appearanceEditor_.Draw(editSession_.GetStatusMessage())) {
		nodePreviews_.ClearNodePreviews();
	}
}

bool Engine::ShaderGraphEditorTool::RequestGraphCreation(const EditorToolContext& context) {

	if (!editSession_.IsDirty()) {
		return CreateGraph(context);
	}
	// 未保存のGraphを解決してから作成する
	requestGraphCreate_ = true;
	requestAssetSwitch_ = false;
	requestWindowClose_ = false;
	requestUnsavedPrompt_ = true;
	return true;
}

bool Engine::ShaderGraphEditorTool::SaveGraph(const EditorToolContext& context) {

	// Canvasの配置を保存用Graphへ取り込む
	CaptureNodePositions();
	return editSession_.Save(context);
}
