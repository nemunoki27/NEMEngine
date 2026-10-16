#include "IEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <imgui_internal.h>

using namespace Engine;

void Engine::IEditorTool::BeginEditorToolFrame(const EditorToolContext& context) {

	renderResources_.BeginEditorToolFrame(context);
}

void Engine::IEditorTool::EndEditorToolFrame() {

	renderResources_.EndEditorToolFrame();
}

std::string Engine::IEditorTool::MakeWindowName(const char* identity, const char* previousName) const {

	const std::string name = GetDescriptor().name + "###" + identity;
	const ImGuiID windowID = ImHashStr(name.c_str());
	if (!ImGui::FindWindowByID(windowID) && !ImGui::FindWindowSettingsByID(windowID)) {
		// 日本語化前の位置とドッキング先を新しい表示名へ移す
		if (const auto* oldSettings = ImGui::FindWindowSettingsByID(ImHashStr(previousName ? previousName : identity))) {
			const ImGuiWindowSettings saved = *oldSettings;
			auto* settings = ImGui::CreateNewWindowSettings(name.c_str());
			*settings = saved;
			settings->ID = windowID;
			settings->WantApply = true;
			settings->WantDelete = false;
		}
	}
	return name;
}

EditorToolRenderTexture* Engine::IEditorTool::CreateRenderTexture(const std::string& name,
	const Vector2I& size, const Color4& clearColor, uint32_t colorCount,
	bool withDepth) {

	return renderResources_.CreateRenderTexture(name, size, clearColor, colorCount, withDepth);
}

EditorToolRenderTexture* Engine::IEditorTool::FindRenderTexture(const std::string& name) {

	return renderResources_.FindRenderTexture(name);
}

const EditorToolRenderTexture* Engine::IEditorTool::FindRenderTexture(const std::string& name) const {

	return renderResources_.FindRenderTexture(name);
}

void Engine::IEditorTool::DestroyRenderTexture(const std::string& name) {

	renderResources_.DestroyRenderTexture(name);
}

void Engine::IEditorTool::ClearRenderTextures() {

	renderResources_.ClearRenderTextures();
}

bool Engine::IEditorTool::AcceptPreviewEntityDragDrop(EditorToolRenderTexture& texture) const {

	return renderResources_.AcceptPreviewEntityDragDrop(texture);
}

bool Engine::IEditorTool::AcceptPreviewEntityDragDrop(const EditorToolContext& context,
	EditorToolRenderTexture& texture) const {

	return renderResources_.AcceptPreviewEntityDragDrop(context, texture);
}

Entity Engine::IEditorTool::GetPreviewEntity(const EditorToolRenderTexture& texture) const {

	return renderResources_.GetPreviewEntity(texture);
}

Entity Engine::IEditorTool::GetPreviewEntity(const EditorToolContext& context, const EditorToolRenderTexture& texture) const {

	return renderResources_.GetPreviewEntity(context, texture);
}
