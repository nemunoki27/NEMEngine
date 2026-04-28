#include "ToolPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ToolRegistry.h>
#include <Engine/Editor/Tools/IEditorTool.h>

namespace {

	// EditorPanelContextからツール共通のコンテキストを作る
	Engine::ToolContext MakeToolContext(const Engine::EditorPanelContext& context) {

		Engine::ToolContext toolContext{};
		if (!context.editorContext) {
			return toolContext;
		}

		toolContext.world = context.editorContext->activeWorld;
		toolContext.assetDatabase = context.editorContext->assetDatabase;
		toolContext.activeSceneHeader = context.editorContext->activeSceneHeader;
		toolContext.activeSceneAsset = context.editorContext->activeSceneAsset;
		toolContext.activeSceneInstanceID = context.editorContext->activeSceneInstanceID;
		toolContext.activeScenePath = context.editorContext->activeScenePath;
		toolContext.isPlaying = context.editorContext->isPlaying;
		toolContext.canEditScene = context.CanEditScene();
		return toolContext;
	}

	// ツール所有元の表示名を返す
	const char* ToOwnerLabel(Engine::ToolOwner owner) {

		switch (owner) {
		case Engine::ToolOwner::Game: return "Game";
		case Engine::ToolOwner::Engine:
		default:
			return "Engine";
		}
	}

	// ツールの説明ツールチップを表示する
	void DrawToolTooltip(const Engine::ITool& tool, const Engine::ToolContext& toolContext) {

		const Engine::ToolDescriptor& desc = tool.GetDescriptor();
		ImGui::TextDisabled("[%s]", ToOwnerLabel(desc.owner));
		if (!desc.description.empty()) {
			ImGui::TextWrapped("%s", desc.description.c_str());
		}
		if (!tool.IsEnabled(toolContext)) {
			ImGui::Separator();
			ImGui::TextDisabled("This tool is not available in the current mode.");
		}
	}
}

//============================================================================
//	ToolPanel classMethods
//============================================================================

void Engine::ToolPanel::Draw(const EditorPanelContext& context) {

	// ツールパネルの表示状態を確認
	if (!context.layoutState->showTool) {
		return;
	}

	if (!ImGui::Begin("Tool", &context.layoutState->showTool)) {
		ImGui::End();
		return;
	}

	ToolRegistry& registry = ToolRegistry::GetInstance();
	std::vector<ITool*> tools = registry.GetTools();
	ToolContext toolContext = MakeToolContext(context);

	ImGui::SetWindowFontScale(0.72f);

	// まだツールが登録されていない場合は導線だけ表示する
	if (tools.empty()) {

		ImGui::TextDisabled("No tools registered.");
		ImGui::TextWrapped("Engine and Game tools can be registered from C++ through ToolRegistry.");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
		return;
	}

	if (!registry.Find(activeToolID_)) {
		activeToolID_ = tools.front()->GetDescriptor().id;
	}

	// カテゴリ付きのツール一覧を表示する
	ImGui::BeginChild("##ToolList", ImVec2(0.0f, 0.0f), true);

	std::string currentCategory;
	for (ITool* tool : tools) {
		if (!tool) {
			continue;
		}

		const ToolDescriptor& desc = tool->GetDescriptor();
		if (currentCategory != desc.category) {

			currentCategory = desc.category;
			if (ImGui::GetCursorPosY() > 0.0f) {
				ImGui::Spacing();
			}
			ImGui::TextDisabled("%s", currentCategory.c_str());
			ImGui::Separator();
		}

		const bool selected = activeToolID_ == desc.id;
		const bool enabled = tool->IsEnabled(toolContext);
		if (!enabled) {
			ImGui::BeginDisabled();
		}

		if (ImGui::Selectable(desc.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
			activeToolID_ = desc.id;
		}
		if (enabled && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			// ダブルクリックでツール側の独立ウィンドウを開く
			if (auto* editorTool = dynamic_cast<IEditorTool*>(tool)) {
				editorTool->OpenEditorTool();
			}
		}
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		if (!enabled) {
			ImGui::EndDisabled();
		}
		if (hovered && ImGui::BeginItemTooltip()) {
			DrawToolTooltip(*tool, toolContext);
			ImGui::EndTooltip();
		}
	}
	ImGui::EndChild();

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();

	// 開いているツールウィンドウはToolPanel本体とは独立して描画する
	EditorToolContext editorToolContext{};
	editorToolContext.panelContext = &context;
	editorToolContext.toolContext = toolContext;
	for (ITool* tool : tools) {
		if (auto* editorTool = dynamic_cast<IEditorTool*>(tool)) {
			editorTool->DrawEditorTool(editorToolContext);
		}
	}
}
