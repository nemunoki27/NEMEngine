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

	// 選択中ツールの概要を表示する
	void DrawToolSummary(const Engine::ITool& tool, const Engine::ToolContext& toolContext) {

		const Engine::ToolDescriptor& desc = tool.GetDescriptor();
		ImGui::TextUnformatted(desc.name.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("[%s]", ToOwnerLabel(desc.owner));

		if (!desc.description.empty()) {
			ImGui::TextWrapped("%s", desc.description.c_str());
		}

		if (!tool.IsEnabled(toolContext)) {
			ImGui::Spacing();
			ImGui::TextDisabled("This tool is not available in the current mode.");
		}
		ImGui::Separator();
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

	// まだツールが登録されていない場合は導線だけ表示する
	if (tools.empty()) {

		ImGui::TextDisabled("No tools registered.");
		ImGui::TextWrapped("Engine and Game tools can be registered from C++ through ToolRegistry.");
		ImGui::End();
		return;
	}

	if (!registry.Find(activeToolID_)) {
		activeToolID_ = tools.front()->GetDescriptor().id;
	}

	// 左側にカテゴリ付きのツール一覧を表示する
	const float listWidth = 220.0f;
	ImGui::BeginChild("##ToolList", ImVec2(listWidth, 0.0f), true);

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
		if (ImGui::Selectable(desc.name.c_str(), selected)) {
			activeToolID_ = desc.id;
		}
		if (ImGui::IsItemHovered() && !desc.description.empty()) {
			ImGui::SetTooltip("%s", desc.description.c_str());
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("##ToolBody", ImVec2(0.0f, 0.0f), true);

	// 右側に選択中ツールの詳細と編集UIを表示する
	ITool* activeTool = registry.Find(activeToolID_);
	if (activeTool) {

		DrawToolSummary(*activeTool, toolContext);

		if (auto* editorTool = dynamic_cast<IEditorTool*>(activeTool)) {

			EditorToolContext editorToolContext{};
			editorToolContext.panelContext = &context;
			editorToolContext.toolContext = toolContext;
			editorTool->DrawEditorTool(editorToolContext);
		} else {

			ImGui::TextDisabled("This tool has no editor UI.");
		}
	}
	ImGui::EndChild();

	ImGui::End();
}
