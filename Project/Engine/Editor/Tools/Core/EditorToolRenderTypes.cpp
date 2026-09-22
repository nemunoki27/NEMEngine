#include "EditorToolRenderTypes.h"

using namespace Engine;

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
