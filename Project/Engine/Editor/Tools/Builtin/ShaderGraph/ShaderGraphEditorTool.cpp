#include "ShaderGraphEditorTool.h"
#include "ShaderGraphCanvasID.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <filesystem>
#include <utility>

#include <imgui.h>
#include <imgui_node_editor.h>

using namespace Engine::ShaderGraphAppearance;
using namespace Engine::ShaderGraphCanvasID;

namespace {

	namespace ed = ax::NodeEditor;

}

//============================================================================
//	ShaderGraphEditorTool classMethods
//============================================================================

Engine::ShaderGraphEditorTool::ShaderGraphEditorTool()
	: nodeTransfer_(editSession_), nodeDrawer_(editSession_, appearanceEditor_.GetSettings(), nodePreviews_),
	  groupEditor_(editSession_, appearanceEditor_.GetSettings()),
	  canvasMenu_(editSession_, canvas_, groupEditor_, nodeTransfer_),
	  canvasView_(editSession_, canvas_, appearanceEditor_, nodePreviews_, nodeDrawer_, groupEditor_, canvasMenu_),
	  previewController_(editSession_, scenePreview_), settingsEditor_(editSession_, previewController_) {

	nodePreviews_.Init();
}

Engine::ShaderGraphEditorTool::~ShaderGraphEditorTool() {

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

	commandPanelFocused_ = false;
	if (pendingAsset_) {
		RequestGraphSwitch(context, pendingAsset_);
		pendingAsset_ = {};
	}
	if (openWindow_) {
		DrawWindow(context);
	}
	DrawUnsavedPrompt(context);
	if (!openWindow_) {
		RestorePreviewMaterial(context);
		return;
	}
	previewController_.Update(context);
}

void Engine::ShaderGraphEditorTool::DrawWindow(const EditorToolContext& context) {

	const bool wasOpen = openWindow_;
	const bool visible = ImGui::Begin("シェーダーグラフ", &openWindow_, ImGuiWindowFlags_MenuBar);
	commandPanelFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (!visible) {

		ImGui::End();
		if (wasOpen && !openWindow_) {
			if (editSession_.IsDirty()) {
				openWindow_ = true;
				requestWindowClose_ = true;
				requestUnsavedPrompt_ = true;
			} else {
				RestorePreviewMaterial(context);
			}
		}
		return;
	}

	toolbar_.Draw(context, editSession_, *this);
	ImGui::Separator();

	const float panelWidth = (std::min)(360.0f, ImGui::GetContentRegionAvail().x * 0.35f);
	if (ImGui::BeginChild(
			"ShaderGraphParameters", ImVec2(panelWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {

		DrawParameterPanel(context);
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if (ImGui::BeginChild("ShaderGraphCanvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {

		DrawGraph(context);
	}
	ImGui::EndChild();
	if (editSession_.IsLoaded() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {

		CommitGraphHistory();
	}
	ImGui::End();
	if (wasOpen && !openWindow_) {
		if (editSession_.IsDirty()) {
			openWindow_ = true;
			requestWindowClose_ = true;
			requestUnsavedPrompt_ = true;
		} else {
			RestorePreviewMaterial(context);
		}
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
		openWindow_ = true;
		pendingEditCloseResult_ = EditorToolCloseResult::Cancelled;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Engine::ShaderGraphEditorTool::DrawGraphSettings(const EditorToolContext& context) {

	if (settingsEditor_.Draw(context)) {
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
	RestorePreviewMaterial(context);
	// プレビューの自動保存で取り込み直後の内容を確定しない
	previewController_.ResetTarget();
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

	RestorePreviewMaterial(context);
	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database) {
		editSession_.GetStatusMessage() = "作成先を設定してください";
		return false;
	}
	const AssetID assetID =
		ShaderGraphAssetAuthoring::Create(*database, toolbar_.GetCreationRequest(), editSession_.GetStatusMessage());
	return assetID && LoadGraph(context, assetID);
}

void Engine::ShaderGraphEditorTool::RestorePreviewMaterial(const EditorToolContext& context) {

	previewController_.Restore(context);
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

	RestorePreviewMaterial(context);
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
		RestorePreviewMaterial(context);
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

	RestorePreviewMaterial(context);
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
		openWindow_ = false;
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

void Engine::ShaderGraphEditorTool::DrawParameterPanel(const EditorToolContext& context) {

	DrawAppearancePanel();
	if (!editSession_.IsLoaded()) {
		return;
	}

	ImGui::Separator();
	DrawGraphSettings(context);

	if (editSession_.GetDraft().domain == ShaderGraphDomain::Surface) {
		previewController_.DrawSettings(context);
	}

	parameterEditor_.Draw(context, editSession_);
	keywordEditor_.Draw(editSession_);
	DrawSelectedNodeEditor(context);
	canvasView_.DrawDiagnostics();
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

void Engine::ShaderGraphEditorTool::RequestGraphCreation(const EditorToolContext& context) {

	if (!editSession_.IsDirty()) {
		CreateGraph(context);
		return;
	}
	// 未保存のGraphを解決してから作成する
	requestGraphCreate_ = true;
	requestAssetSwitch_ = false;
	requestWindowClose_ = false;
	requestUnsavedPrompt_ = true;
}

bool Engine::ShaderGraphEditorTool::SaveGraph(const EditorToolContext& context) {

	// Canvasの配置を保存用Graphへ取り込む
	CaptureNodePositions();
	return editSession_.Save(context);
}
