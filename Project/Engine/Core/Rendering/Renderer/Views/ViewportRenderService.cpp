#include "ViewportRenderService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>

//============================================================================
//	ViewportRenderService classMethods
//============================================================================
Engine::ViewportRenderService::~ViewportRenderService() {

	Finalize();
}

Engine::MultiRenderTargetCreateDesc Engine::ViewportRenderService::BuildDefaultDesc(
	RenderViewKind kind, uint32_t width, uint32_t height) {

	MultiRenderTargetCreateDesc desc{};
	desc.width = width;
	desc.height = height;

	ColorAttachmentDesc color{};
	color.name = GetPrimaryColorName(kind);
	color.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	color.clearColor = Engine::Color4::FromHex(0x303030ff);
	// 色変更時も描画先を再生成せずクリアする
	color.useOptimizedClearValue = false;
	color.createUAV = false;
	desc.colors.emplace_back(color);

	DepthTextureCreateDesc depth{};
	depth.width = width;
	depth.height = height;
	depth.resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
	depth.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth.srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depth.debugName = (kind == RenderViewKind::Game) ? L"GameDepth" : L"SceneViewDepth";
	desc.depth = depth;

	return desc;
}

Engine::MultiRenderTargetCreateDesc Engine::ViewportRenderService::BuildRenderTextureDesc(
	AssetID assetID, uint32_t width, uint32_t height) {

	MultiRenderTargetCreateDesc desc = BuildDefaultDesc(RenderViewKind::Game, width, height);
	const std::string name = "RenderTexture_" + ToString(assetID);
	desc.colors[0].name = name;
	desc.depth->debugName = std::wstring(name.begin(), name.end()) + L"_Depth";
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

	// 作成が終わるまでは旧描画先を維持する
	MultiRenderTargetCreateDesc desc = BuildDefaultDesc(kind, width, height);

	auto candidate = std::make_unique<MultiRenderTarget>();
	candidate->Create(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(), &graphicsCore.GetSRVDescriptor(), desc);
	slot.surface.swap(candidate);
	slot.width = width;
	slot.height = height;
}

void Engine::ViewportRenderService::SyncRenderTextureSurface(GraphicsCore& graphicsCore,
	AssetID assetID, uint32_t width, uint32_t height) {

	SyncRenderTextureSurface({graphicsCore.GetDXObject().GetDevice(), graphicsCore.GetRTVDescriptor(),
		graphicsCore.GetDSVDescriptor(), graphicsCore.GetSRVDescriptor()}, assetID, width, height);
}

void Engine::ViewportRenderService::SyncRenderTextureSurface(const RenderTargetCreationContext& context,
	AssetID assetID, uint32_t width, uint32_t height) {

	if (!assetID || width == 0 || height == 0) {
		return;
	}

	SurfaceSlot& slot = renderTextures_[assetID];
	slot.lastUsedSerial = GraphicsFrameState::GetFrameSerial();
	if (slot.surface && slot.width == width && slot.height == height) {
		return;
	}

	// 新しい出力Textureを作成する
	auto candidate = std::make_unique<MultiRenderTarget>();
	candidate->Create(context.device, &context.targets, &context.depths, &context.shaders, BuildRenderTextureDesc(assetID, width, height));
	// 公開に成功してから所有とサイズを切り替える
	auto ownership = std::make_shared<const uint8_t>(0);
	RuntimeTextureResolver::RegisterRenderTexture(assetID, candidate->GetColorTexture(0), ownership);
	slot.surface.swap(candidate);
	slot.ownership = std::move(ownership);
	slot.width = width;
	slot.height = height;
}

void Engine::ViewportRenderService::CollectRenderTextures(std::span<const ResolvedRenderView> cameras) {

	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	for (const auto& camera : cameras) {
		const auto found = renderTextures_.find(camera.targetTexture);
		if (found != renderTextures_.end()) found->second.lastUsedSerial = serial;
	}
	for (auto it = renderTextures_.begin(); it != renderTextures_.end();) {
		auto& slot = it->second;
		if (!slot.surface) { it = renderTextures_.erase(it); continue; }
		// 公開側と所有元以外の借用はMaterialのCacheにも残る
		if (slot.ownership.use_count() > 2 || !HasExpiredGraphicsResource(slot.lastUsedSerial, serial) ||
			!HasExpiredGraphicsResource(RuntimeTextureResolver::GetLastRenderTextureUse(it->first), serial)) {
			++it;
			continue;
		}
		RuntimeTextureResolver::UnregisterRenderTexture(it->first, slot.surface->GetColorTexture(0));
		ReleaseSlot(slot);
		++collectedRenderTextures_;
		it = renderTextures_.erase(it);
	}
}

void Engine::ViewportRenderService::Finalize() {

	ReleaseSlot(game_);
	ReleaseSlot(scene_);
	for (auto& [assetID, slot] : renderTextures_) {
		if (slot.surface) {
			RuntimeTextureResolver::UnregisterRenderTexture(
				assetID, slot.surface->GetColorTexture(0));
		}
		ReleaseSlot(slot);
	}
	renderTextures_.clear();
}

std::shared_ptr<const uint8_t> Engine::ViewportRenderService::GetRenderTextureOwnership(AssetID assetID) const {

	const auto found = renderTextures_.find(assetID);
	return found != renderTextures_.end() ? found->second.ownership : nullptr;
}

Engine::MultiRenderTarget* Engine::ViewportRenderService::GetSurface(RenderViewKind kind) {

	return GetSlot(kind).surface.get();
}

const Engine::MultiRenderTarget* Engine::ViewportRenderService::GetSurface(RenderViewKind kind) const {

	return GetSlot(kind).surface.get();
}

Engine::MultiRenderTarget* Engine::ViewportRenderService::GetRenderTextureSurface(AssetID assetID) {

	const auto found = renderTextures_.find(assetID);
	return found != renderTextures_.end() ? found->second.surface.get() : nullptr;
}

const Engine::MultiRenderTarget* Engine::ViewportRenderService::GetRenderTextureSurface(AssetID assetID) const {

	const auto found = renderTextures_.find(assetID);
	return found != renderTextures_.end() ? found->second.surface.get() : nullptr;
}

Engine::ViewportRenderService::SurfaceSlot& Engine::ViewportRenderService::GetSlot(RenderViewKind kind) {

	return kind == RenderViewKind::Game ? game_ : scene_;
}

const Engine::ViewportRenderService::SurfaceSlot& Engine::ViewportRenderService::GetSlot(RenderViewKind kind) const {

	return kind == RenderViewKind::Game ? game_ : scene_;
}

void Engine::ViewportRenderService::ReleaseSlot(SurfaceSlot& slot) {

	if (slot.surface) {
		// 描画先の資源を回収へ渡す
		slot.surface->Destroy();
		slot.surface.reset();
	}
	slot.ownership.reset();
	slot.width = 0;
	slot.height = 0;
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

	// SceneView表示サーフェスの深度名はGBufferのSceneMain深度"SceneDepth"と衝突させない
	return (kind == RenderViewKind::Game) ? "GameDepth" : "SceneViewDepth";
}
