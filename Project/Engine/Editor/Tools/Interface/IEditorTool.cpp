#include "IEditorTool.h"

using namespace Engine;

//============================================================================
//	IEditorTool classMethods
//============================================================================

void Engine::EditorToolRenderTexture::Destroy() {

	if (renderTarget) {

		renderTarget->Destroy();
		renderTarget.reset();
	}
	size.Init();
	colorCount = 1;
	clearColor = Color4::Black();
	previewEntityUUID = UUID{};
}

RenderTexture2D* Engine::EditorToolRenderTexture::GetColorTexture(uint32_t index) const {

	if (!IsValid() || renderTarget->GetColorCount() <= index) {
		return nullptr;
	}
	return renderTarget->GetColorTexture(index);
}

ImTextureID Engine::EditorToolRenderTexture::GetImTextureID(uint32_t index) const {

	const RenderTexture2D* texture = GetColorTexture(index);
	if (!texture) {
		return static_cast<ImTextureID>(0);
	}
	return static_cast<ImTextureID>(texture->GetSRVGPUHandle().ptr);
}

void Engine::IEditorTool::BeginEditorToolFrame(const EditorToolContext& context) {

	currentToolContext_ = &context;
	currentGraphicsCore_ = context.panelContext ? context.panelContext->graphicsCore : nullptr;
}

void Engine::IEditorTool::EndEditorToolFrame() {

	currentToolContext_ = nullptr;
	currentGraphicsCore_ = nullptr;
}

EditorToolRenderTexture* Engine::IEditorTool::CreateRenderTexture(const std::string& name,
	const Vector2I& size, const Color4& clearColor, uint32_t colorCount) {

	if (EditorToolRenderTexture* texture = FindRenderTexture(name)) {
		return texture;
	}

	if (!currentGraphicsCore_) {
		return nullptr;
	}

	// テクスチャ設定
	EditorToolRenderTexture texture{};
	texture.name = name;
	texture.size = size;
	texture.colorCount = std::clamp(colorCount, 1u, 8u);
	texture.clearColor = clearColor;
	texture.renderTarget = std::make_unique<MultiRenderTarget>();
	// レンダーターゲット記述子の作成
	MultiRenderTargetCreateDesc createDesc{};
	createDesc.width = size.x;
	createDesc.height = size.y;
	createDesc.colors.reserve(texture.colorCount);
	for (uint32_t i = 0; i < texture.colorCount; ++i) {

		// Meshの標準PSはMRTを使うため、プレビュー側も必要枚数分のRTVを作成する。
		const std::string colorName = i == 0 ? name + "_Color" :
			name + "_Color" + std::to_string(i);
		createDesc.colors.push_back(ColorAttachmentDesc{ .name = colorName,
			.format = DXGI_FORMAT_R8G8B8A8_UNORM,.clearColor = clearColor,.createUAV = false, });
	}

	// ツールプレビューは3D確認で使うことが多いため、必ず深度を持たせる。
	DepthTextureCreateDesc depthDesc{};
	depthDesc.width = size.x;
	depthDesc.height = size.y;
	depthDesc.debugName = std::wstring(name.begin(), name.end()) + L"_Depth";
	createDesc.depth = depthDesc;

	// レンダーターゲット作成
	texture.renderTarget->Create(currentGraphicsCore_->GetDXObject().GetDevice(),
		&currentGraphicsCore_->GetRTVDescriptor(), &currentGraphicsCore_->GetDSVDescriptor(),
		&currentGraphicsCore_->GetSRVDescriptor(), createDesc);

	// ツール上で使うレンダーターゲットを追加
	renderTextures_.emplace_back(std::move(texture));
	return &renderTextures_.back();
}

EditorToolRenderTexture* Engine::IEditorTool::FindRenderTexture(const std::string& name) {

	auto it = std::find_if(renderTextures_.begin(), renderTextures_.end(),
		[&name](const EditorToolRenderTexture& texture) {
			return texture.name == name;
		});
	return it != renderTextures_.end() ? &(*it) : nullptr;
}

const EditorToolRenderTexture* Engine::IEditorTool::FindRenderTexture(const std::string& name) const {

	auto it = std::find_if(renderTextures_.begin(), renderTextures_.end(),
		[&name](const EditorToolRenderTexture& texture) {
			return texture.name == name;
		});
	return it != renderTextures_.end() ? &(*it) : nullptr;
}

void Engine::IEditorTool::DestroyRenderTexture(const std::string& name) {

	auto it = std::find_if(renderTextures_.begin(), renderTextures_.end(),
		[&name](const EditorToolRenderTexture& texture) {
			return texture.name == name;
		});
	if (it == renderTextures_.end()) {
		return;
	}

	it->Destroy();
	renderTextures_.erase(it);
}

void Engine::IEditorTool::ClearRenderTextures() {

	for (EditorToolRenderTexture& texture : renderTextures_) {

		texture.Destroy();
	}
	renderTextures_.clear();
}

bool Engine::IEditorTool::AcceptPreviewEntityDragDrop(EditorToolRenderTexture& texture) const {

	if (!currentToolContext_) {
		return false;
	}
	return AcceptPreviewEntityDragDrop(*currentToolContext_, texture);
}

bool Engine::IEditorTool::AcceptPreviewEntityDragDrop(const EditorToolContext& context,
	EditorToolRenderTexture& texture) const {

	if (!ImGui::BeginDragDropTarget()) {
		return false;
	}

	bool changed = false;
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType)) {
		if (payload->IsDelivery() && payload->DataSize == sizeof(UUID)) {

			const UUID droppedUUID = *static_cast<const UUID*>(payload->Data);
			ECSWorld* world = context.GetWorld();
			const Entity entity = world ? world->FindByUUID(droppedUUID) : Entity::Null();
			if (world && world->IsAlive(entity)) {

				// 元のEntityは操作せず、プレビュー対象のUUIDだけを更新する。
				texture.previewEntityUUID = droppedUUID;
				changed = true;
			}
		}
	}
	ImGui::EndDragDropTarget();

	return changed;
}

Entity Engine::IEditorTool::GetPreviewEntity(const EditorToolRenderTexture& texture) const {
	if (!currentToolContext_) {
		return Entity::Null();
	}
	return GetPreviewEntity(*currentToolContext_, texture);
}

Entity Engine::IEditorTool::GetPreviewEntity(const EditorToolContext& context, const EditorToolRenderTexture& texture) const {

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return Entity::Null();
	}

	const Entity entity = world->FindByUUID(texture.previewEntityUUID);
	return world->IsAlive(entity) ? entity : Entity::Null();
}
