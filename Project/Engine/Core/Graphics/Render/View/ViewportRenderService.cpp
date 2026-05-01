#include "ViewportRenderService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>

//============================================================================
//	ViewportRenderService classMethods
//============================================================================

Engine::MultiRenderTargetCreateDesc Engine::ViewportRenderService::BuildDefaultDesc(
	RenderViewKind kind, uint32_t width, uint32_t height) {

	MultiRenderTargetCreateDesc desc{};
	desc.width = width;
	desc.height = height;

	ColorAttachmentDesc color{};
	color.name = GetPrimaryColorName(kind);
	color.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	color.clearColor = Engine::Color4::FromHex(0x303030ff);
	color.createUAV = false;
	desc.colors.emplace_back(color);

	DepthTextureCreateDesc depth{};
	depth.width = width;
	depth.height = height;
	depth.resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
	depth.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth.srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depth.debugName = (kind == RenderViewKind::Game) ? L"GameDepth" : L"SceneDepth";
	desc.depth = depth;

	return desc;
}

void Engine::ViewportRenderService::SyncSurface(GraphicsCore& graphicsCore,
	RenderViewKind kind, uint32_t width, uint32_t height) {

	if (width == 0 || height == 0) {
		return;
	}

	SurfaceSlot& slot = GetSlot(kind);

	const bool needsRecreate =
		!slot.surface ||
		slot.width != width ||
		slot.height != height;

	if (!needsRecreate) {
		return;
	}

	slot.width = width;
	slot.height = height;

	MultiRenderTargetCreateDesc desc = BuildDefaultDesc(kind, width, height);

	slot.surface = std::make_unique<MultiRenderTarget>();
	slot.surface->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		desc);
}

Engine::MultiRenderTarget* Engine::ViewportRenderService::GetSurface(RenderViewKind kind) {

	return GetSlot(kind).surface.get();
}

const Engine::MultiRenderTarget* Engine::ViewportRenderService::GetSurface(RenderViewKind kind) const {

	return GetSlot(kind).surface.get();
}

Engine::ViewportRenderService::SurfaceSlot& Engine::ViewportRenderService::GetSlot(RenderViewKind kind) {

	return kind == RenderViewKind::Game ? game_ : scene_;
}

const Engine::ViewportRenderService::SurfaceSlot& Engine::ViewportRenderService::GetSlot(RenderViewKind kind) const {

	return kind == RenderViewKind::Game ? game_ : scene_;
}

Engine::RenderTexture2D* Engine::ViewportRenderService::GetDisplayTexture(RenderViewKind kind, size_t colorIndex) {

	MultiRenderTarget* surface = GetSurface(kind);
	if (!surface || colorIndex >= surface->GetColorCount()) {
		return nullptr;
	}
	return surface->GetColorTexture(colorIndex);
}

const Engine::RenderTexture2D* Engine::ViewportRenderService::GetDisplayTexture(RenderViewKind kind, size_t colorIndex) const {

	const MultiRenderTarget* surface = GetSurface(kind);
	if (!surface || colorIndex >= surface->GetColorCount()) {
		return nullptr;
	}
	return surface->GetColorTexture(colorIndex);
}

const char* Engine::ViewportRenderService::GetViewAlias(RenderViewKind kind) {

	return (kind == RenderViewKind::Game) ? "GameView" : "SceneView";
}

const char* Engine::ViewportRenderService::GetPrimaryColorName(RenderViewKind kind) {

	return (kind == RenderViewKind::Game) ? "GameColor" : "SceneColor";
}

const char* Engine::ViewportRenderService::GetPrimaryDepthName(RenderViewKind kind) {

	return (kind == RenderViewKind::Game) ? "GameDepth" : "SceneDepth";
}