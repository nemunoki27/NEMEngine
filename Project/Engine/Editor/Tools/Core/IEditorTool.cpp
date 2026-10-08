#include "IEditorTool.h"

using namespace Engine;

void Engine::IEditorTool::BeginEditorToolFrame(const EditorToolContext& context) {

	renderResources_.BeginEditorToolFrame(context);
}

void Engine::IEditorTool::EndEditorToolFrame() {

	renderResources_.EndEditorToolFrame();
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
