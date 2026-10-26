#include "RenderPathResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetSizing.h>

// c++
#include <string>
#include <type_traits>
#include <utility>

namespace {

	// 所有元の読み書きに合わせて色Textureを取得する
	template<typename T>
	auto ResolveGBufferColor(T* surface, Engine::GBufferAttachment attachment) {

		uint32_t index = static_cast<uint32_t>(attachment);
		return surface ? surface->GetColorTexture(index) : nullptr;
	}
}

//============================================================================
//	ScreenSpaceOutlineViewResources classMethods
//============================================================================
bool Engine::ScreenSpaceOutlineViewResources::IsValid() const {

	return mask_ && mask_->IsValid() &&
		projectedCoverageMask_ && projectedCoverageMask_->IsValid() &&
		horizontalDilatedMask_ && horizontalDilatedMask_->IsValid() &&
		dilatedMask_ && dilatedMask_->IsValid();
}

void Engine::ScreenSpaceOutlineViewResources::Destroy() {

	// 各Maskの所有を解除する
	if (mask_) {
		mask_->Destroy();
		mask_.reset();
	}
	if (projectedCoverageMask_) {
		projectedCoverageMask_->Destroy();
		projectedCoverageMask_.reset();
	}
	if (horizontalDilatedMask_) {
		horizontalDilatedMask_->Destroy();
		horizontalDilatedMask_.reset();
	}
	if (dilatedMask_) {
		dilatedMask_->Destroy();
		dilatedMask_.reset();
	}
}

//============================================================================
//	RenderPathResources classMethods
//============================================================================
void Engine::RenderPathResources::Resize(GraphicsCore& graphicsCore, uint32_t width, uint32_t height, DXGI_FORMAT albedoFormat) {

	// 不正なサイズや変更なしなら生成を省く
	if (!NeedsResize(width, height, albedoFormat)) {
		return;
	}
	RenderTargetCreationContext context{graphicsCore.GetDXObject().GetDevice(), graphicsCore.GetRTVDescriptor(),
		graphicsCore.GetDSVDescriptor(), graphicsCore.GetSRVDescriptor()};
	Resize(context, width, height, albedoFormat);
}

void Engine::RenderPathResources::Resize(const RenderTargetCreationContext& context, uint32_t width, uint32_t height, DXGI_FORMAT albedoFormat) {

	if (!NeedsResize(width, height, albedoFormat)) {
		return;
	}
	// 全描画先が完成してからサイズと所有を切り替える
	RenderPathResources candidate;
	candidate.Create(context, width, height, albedoFormat);
	candidate.albedoFormat_ = albedoFormat;
	candidate.currentWidth_ = width;
	candidate.currentHeight_ = height;
	// 既に使用したOutlineも候補側で完成させる
	if (runtimeOutline_.IsValid()) candidate.PrepareScreenSpaceOutline(context, false);
	if (editorSelectionOutline_.IsValid()) candidate.PrepareScreenSpaceOutline(context, true);
	Swap(candidate);
}

bool Engine::RenderPathResources::NeedsResize(uint32_t width, uint32_t height, DXGI_FORMAT albedoFormat) const {

	return width != 0 && height != 0 && RenderTargetSizing::ResolveSize(width, height).has_value() &&
		(width != currentWidth_ || height != currentHeight_ || albedoFormat != albedoFormat_ || !IsValid());
}

void Engine::RenderPathResources::Swap(RenderPathResources& other) noexcept {

	// GPU資源と確定寸法をまとめて入れ替える
	static_assert(std::is_nothrow_swappable_v<ScreenSpaceOutlineViewResources>);
	sceneMain_.swap(other.sceneMain_);
	sceneFinal_.swap(other.sceneFinal_);
	sceneColorOpaque_.swap(other.sceneColorOpaque_);
	depthPyramid_.Swap(other.depthPyramid_);
	particleOpaqueDepth_.Swap(other.particleOpaqueDepth_);
	std::swap(runtimeOutline_, other.runtimeOutline_);
	std::swap(editorSelectionOutline_, other.editorSelectionOutline_);
	std::swap(currentWidth_, other.currentWidth_);
	std::swap(currentHeight_, other.currentHeight_);
	std::swap(albedoFormat_, other.albedoFormat_);
}

void Engine::RenderPathResources::Create(const RenderTargetCreationContext& context, uint32_t width, uint32_t height, DXGI_FORMAT albedoFormat) {

	// 同じDeviceとDescriptorで各描画先を生成する
	auto createSurface = [&](const MultiRenderTargetCreateDesc& desc) {

		auto surface = std::make_unique<MultiRenderTarget>();
		surface->Create(context.device, &context.targets, &context.depths, &context.shaders, desc);
		return surface;
	};
	// GBuffer・合成先・透明描画用の背景を生成する
	sceneMain_ = createSurface(BuildSceneMainDesc(width, height, albedoFormat));
	sceneFinal_ = createSurface(BuildSceneFinalDesc(width, height));
	sceneColorOpaque_ = createSurface(BuildSceneColorOpaqueDesc(width, height));
	depthPyramid_.Create(context.device, &context.shaders, width, height);

}

Engine::ScreenSpaceOutlineViewResources& Engine::RenderPathResources::PrepareScreenSpaceOutline(
	GraphicsCore& graphicsCore, bool editorSelection) {

	const RenderTargetCreationContext context{graphicsCore.GetDXObject().GetDevice(), graphicsCore.GetRTVDescriptor(),
		graphicsCore.GetDSVDescriptor(), graphicsCore.GetSRVDescriptor()};
	return PrepareScreenSpaceOutline(context, editorSelection);
}

Engine::ScreenSpaceOutlineViewResources& Engine::RenderPathResources::PrepareScreenSpaceOutline(
	const RenderTargetCreationContext& context, bool editorSelection) {

	auto& outline = editorSelection ? editorSelectionOutline_ : runtimeOutline_;
	if (outline.IsValid() || !IsValid()) return outline;
	const std::string prefix = editorSelection ? "SSOutline.EditorSelection" : "SSOutline.Runtime";
	// 全Maskの生成が成功してから所有を渡す
	ScreenSpaceOutlineViewResources candidate;
	auto createMask = [&](const char* suffix, bool createUAV) {

		auto surface = std::make_unique<MultiRenderTarget>();
		surface->Create(context.device, &context.targets, &context.depths, &context.shaders,
			BuildScreenSpaceOutlineMaskDesc(currentWidth_, currentHeight_, prefix + suffix, createUAV));
		return surface;
	};
	candidate.mask_ = createMask(".Mask", false);
	candidate.projectedCoverageMask_ = createMask(".ProjectedCoverageMask", false);
	candidate.horizontalDilatedMask_ = createMask(".HorizontalDilatedMask", true);
	candidate.dilatedMask_ = createMask(".FinalDilatedMask", true);
	std::swap(outline, candidate);
	return outline;
}

void Engine::RenderPathResources::Destroy() {

	// Viewに属するGPU資源を回収へ渡す
	if (sceneMain_) {
		sceneMain_->Destroy();
		sceneMain_.reset();
	}
	if (sceneFinal_) {
		sceneFinal_->Destroy();
		sceneFinal_.reset();
	}
	if (sceneColorOpaque_) {
		sceneColorOpaque_->Destroy();
		sceneColorOpaque_.reset();
	}
	depthPyramid_.Destroy();
	particleOpaqueDepth_.Destroy();
	runtimeOutline_.Destroy();
	editorSelectionOutline_.Destroy();
	currentWidth_ = 0;
	currentHeight_ = 0;
}

Engine::MultiRenderTargetCreateDesc Engine::RenderPathResources::BuildSceneMainDesc(uint32_t width, uint32_t height, DXGI_FORMAT albedoFormat) {

	MultiRenderTargetCreateDesc desc{};
	desc.width = width;
	desc.height = height;

	// SceneColorMain
	ColorAttachmentDesc color0{};
	color0.name = RenderTargetNames::kSceneColorMain;
	color0.format = albedoFormat;
	color0.clearColor = Color4::FromHex(0x303030ff);
	color0.createUAV = false;
	desc.colors.emplace_back(color0);

	// SceneNormalMain
	ColorAttachmentDesc color1{};
	color1.name = RenderTargetNames::kSceneNormalMain;
	color1.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	color1.clearColor = Color4::Black();
	color1.createUAV = false;
	desc.colors.emplace_back(color1);

	// ScenePositionMain
	ColorAttachmentDesc color2{};
	color2.name = RenderTargetNames::kScenePositionMain;
	color2.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	color2.clearColor = Color4::Black();
	color2.createUAV = false;
	desc.colors.emplace_back(color2);

	// Metallic・Roughness・Occlusionを8bitで保持する
	ColorAttachmentDesc color3{};
	color3.name = RenderTargetNames::kSceneMaterialMain;
	color3.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	color3.clearColor = Color4::Black();
	color3.createUAV = false;
	desc.colors.emplace_back(color3);

	// 強度を乗算した発光色をHDRで保持する
	ColorAttachmentDesc color4{};
	color4.name = RenderTargetNames::kSceneEmissiveMain;
	color4.format = DXGI_FORMAT_R11G11B10_FLOAT;
	color4.clearColor = Color4::Black();
	color4.createUAV = false;
	desc.colors.emplace_back(color4);

	// 0クリアで未描画画素のLightingを省く
	ColorAttachmentDesc color5{};
	color5.name = RenderTargetNames::kSceneFlagsMain;
	color5.format = DXGI_FORMAT_R32_UINT;
	color5.clearColor = Color4::Black();
	color5.createUAV = false;
	desc.colors.emplace_back(color5);

	// SceneMotionMain、時間フィルタで前フレーム位置へ再投影する
	ColorAttachmentDesc color6{};
	color6.name = RenderTargetNames::kSceneMotionMain;
	color6.format = DXGI_FORMAT_R16G16_FLOAT;
	color6.clearColor = Color4::Black();
	color6.createUAV = false;
	desc.colors.emplace_back(color6);

	// 深度バッファ
	DepthTextureCreateDesc depth{};
	depth.width = width;
	depth.height = height;
	depth.resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
	depth.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth.srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depth.debugName = L"SceneDepth";
	desc.depth = depth;

	return desc;
}

Engine::MultiRenderTargetCreateDesc Engine::RenderPathResources::BuildSceneFinalDesc(uint32_t width, uint32_t height) {

	MultiRenderTargetCreateDesc desc{};
	desc.width = width;
	desc.height = height;

	// Ray Tracingの書込先をUAV付きで生成する
	ColorAttachmentDesc color{};
	color.name = RenderTargetNames::kSceneColorFinal;
	color.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	color.clearColor = Color4::Black();
	color.createUAV = true;
	desc.colors.emplace_back(color);

	return desc;
}

Engine::MultiRenderTargetCreateDesc Engine::RenderPathResources::BuildSceneColorOpaqueDesc(uint32_t width, uint32_t height) {

	MultiRenderTargetCreateDesc desc = BuildSceneFinalDesc(width, height);
	desc.colors[0].name = RenderTargetNames::kSceneColorOpaque;
	desc.colors[0].createUAV = false;
	return desc;
}

Engine::MultiRenderTargetCreateDesc Engine::RenderPathResources::BuildScreenSpaceOutlineMaskDesc(
	uint32_t width, uint32_t height, std::string_view name, bool createUAV) {

	MultiRenderTargetCreateDesc desc{};
	desc.width = width;
	desc.height = height;

	// Style IDを整数で保持し、0を輪郭なしとしてクリアする
	ColorAttachmentDesc color{};
	color.name = std::string(name);
	color.format = DXGI_FORMAT_R16_UINT;
	color.clearColor = Color4::Black();
	color.createUAV = createUAV;
	desc.colors.emplace_back(color);

	return desc;
}

bool Engine::RenderPathResources::IsValid() const {

	return sceneMain_ && sceneMain_->IsValid() &&
		sceneFinal_ && sceneFinal_->IsValid() &&
		sceneColorOpaque_ && sceneColorOpaque_->IsValid() && depthPyramid_.IsValid();
}

Engine::RenderTexture2D* Engine::RenderPathResources::GetGBufferColor(GBufferAttachment attachment) {

	return ResolveGBufferColor(GetSceneMain(), attachment);
}

const Engine::RenderTexture2D* Engine::RenderPathResources::GetGBufferColor(GBufferAttachment attachment) const {

	return ResolveGBufferColor(GetSceneMain(), attachment);
}

Engine::ScreenSpaceOutlineViewResources& Engine::RenderPathResources::GetEditorSelectionScreenSpaceOutline() {

	return editorSelectionOutline_;
}

const Engine::ScreenSpaceOutlineViewResources& Engine::RenderPathResources::GetEditorSelectionScreenSpaceOutline() const {

	return editorSelectionOutline_;
}
