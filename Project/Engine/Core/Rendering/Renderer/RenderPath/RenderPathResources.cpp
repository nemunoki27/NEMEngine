#include "RenderPathResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>

// c++
#include <algorithm>
#include <string>

//============================================================================
//	ScreenSpaceOutlineViewResources classMethods
//============================================================================
bool Engine::ScreenSpaceOutlineViewResources::IsValid() const {

	return mask && mask->IsValid() &&
		projectedCoverageMask && projectedCoverageMask->IsValid() &&
		horizontalDilatedMask && horizontalDilatedMask->IsValid() &&
		dilatedMask && dilatedMask->IsValid();
}

void Engine::ScreenSpaceOutlineViewResources::Destroy() {

	if (mask) {
		mask->Destroy();
		mask.reset();
	}
	if (projectedCoverageMask) {
		projectedCoverageMask->Destroy();
		projectedCoverageMask.reset();
	}
	if (horizontalDilatedMask) {
		horizontalDilatedMask->Destroy();
		horizontalDilatedMask.reset();
	}
	if (dilatedMask) {
		dilatedMask->Destroy();
		dilatedMask.reset();
	}
}

//============================================================================
//	RenderPathResources classMethods
//============================================================================
void Engine::RenderPathResources::Resize(GraphicsCore& graphicsCore, uint32_t width, uint32_t height) {

	if (width == 0 || height == 0) {
		return;
	}
	if (width == currentWidth_ && height == currentHeight_ && IsValid()) {
		return;
	}

	// 旧RTを参照中のフレームを完了させてからDescriptorとResourceを再利用する
	if (IsValid()) {
		graphicsCore.GetDXObject().WaitForGPU();
	}
	currentWidth_ = width;
	currentHeight_ = height;

	// Descriptorを超過しないよう、旧リソースを先に破棄してから新規作成
	if (sceneMain_) {
		sceneMain_->Destroy();
	}
	if (sceneFinal_) {
		sceneFinal_->Destroy();
	}
	depthPyramid_.Destroy();
	runtimeOutline_.Destroy();
	editorSelectionOutline_.Destroy();

	sceneMain_ = std::make_unique<MultiRenderTarget>();
	sceneMain_->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildSceneMainDesc(width, height));

	sceneFinal_ = std::make_unique<MultiRenderTarget>();
	sceneFinal_->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildSceneFinalDesc(width, height));

	depthPyramid_.Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetSRVDescriptor(),
		width, height);

	runtimeOutline_.mask = std::make_unique<MultiRenderTarget>();
	runtimeOutline_.mask->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildScreenSpaceOutlineMaskDesc(width, height, "SSOutline.Runtime.Mask", false));

	runtimeOutline_.projectedCoverageMask = std::make_unique<MultiRenderTarget>();
	runtimeOutline_.projectedCoverageMask->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildScreenSpaceOutlineMaskDesc(width, height, "SSOutline.Runtime.ProjectedCoverageMask", false));

	runtimeOutline_.horizontalDilatedMask = std::make_unique<MultiRenderTarget>();
	runtimeOutline_.horizontalDilatedMask->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildScreenSpaceOutlineMaskDesc(width, height, "SSOutline.Runtime.HorizontalDilatedMask", true));

	runtimeOutline_.dilatedMask = std::make_unique<MultiRenderTarget>();
	runtimeOutline_.dilatedMask->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildScreenSpaceOutlineMaskDesc(width, height, "SSOutline.Runtime.FinalDilatedMask", true));

	editorSelectionOutline_.mask = std::make_unique<MultiRenderTarget>();
	editorSelectionOutline_.mask->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildScreenSpaceOutlineMaskDesc(width, height, "SSOutline.EditorSelection.Mask", false));

	editorSelectionOutline_.projectedCoverageMask = std::make_unique<MultiRenderTarget>();
	editorSelectionOutline_.projectedCoverageMask->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildScreenSpaceOutlineMaskDesc(width, height, "SSOutline.EditorSelection.ProjectedCoverageMask", false));

	editorSelectionOutline_.horizontalDilatedMask = std::make_unique<MultiRenderTarget>();
	editorSelectionOutline_.horizontalDilatedMask->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildScreenSpaceOutlineMaskDesc(width, height, "SSOutline.EditorSelection.HorizontalDilatedMask", true));

	editorSelectionOutline_.dilatedMask = std::make_unique<MultiRenderTarget>();
	editorSelectionOutline_.dilatedMask->Create(
		graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(),
		&graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(),
		BuildScreenSpaceOutlineMaskDesc(width, height, "SSOutline.EditorSelection.FinalDilatedMask", true));
}

void Engine::RenderPathResources::Destroy() {

	if (sceneMain_) {
		sceneMain_->Destroy();
		sceneMain_.reset();
	}
	if (sceneFinal_) {
		sceneFinal_->Destroy();
		sceneFinal_.reset();
	}
	depthPyramid_.Destroy();
	runtimeOutline_.Destroy();
	editorSelectionOutline_.Destroy();
	currentWidth_ = 0;
	currentHeight_ = 0;
}

Engine::MultiRenderTargetCreateDesc Engine::RenderPathResources::BuildSceneMainDesc(uint32_t width, uint32_t height) {

	MultiRenderTargetCreateDesc desc{};
	desc.width = width;
	desc.height = height;

	// SceneColorMain
	ColorAttachmentDesc color0{};
	color0.name = RenderTargetNames::kSceneColorMain;
	color0.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
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

	// SceneMaterialMain、Deferredライティングが参照するmetallic/roughness/occlusionを束ねる
	// rgbに各係数を入れ、8bitで足りる質感パラメータなのでR8G8B8A8で帯域を抑える
	ColorAttachmentDesc color3{};
	color3.name = RenderTargetNames::kSceneMaterialMain;
	color3.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	color3.clearColor = Color4::Black();
	color3.createUAV = false;
	desc.colors.emplace_back(color3);

	// SceneEmissiveMain、自己発光をHDRで保持しライティング加算の初期色に使う
	// intensityを乗算済みの発光色を入れるためalpha不要のR11G11B10で十分
	ColorAttachmentDesc color4{};
	color4.name = RenderTargetNames::kSceneEmissiveMain;
	color4.format = DXGI_FORMAT_R11G11B10_FLOAT;
	color4.clearColor = Color4::Black();
	color4.createUAV = false;
	desc.colors.emplace_back(color4);

	// SceneFlagsMain、マテリアル単位の挙動フラグをライティングパスへ渡す
	// EnableLighting無効画素のスキップ等に使い、0クリアで未描画画素を非ライティング扱いにする
	ColorAttachmentDesc color5{};
	color5.name = RenderTargetNames::kSceneFlagsMain;
	color5.format = DXGI_FORMAT_R32_UINT;
	color5.clearColor = Color4::Black();
	color5.createUAV = false;
	desc.colors.emplace_back(color5);

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

	// SceneColorFinal (UAV付き、RaytracingのDispatchRays書き込み先)
	ColorAttachmentDesc color{};
	color.name = RenderTargetNames::kSceneColorFinal;
	color.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	color.clearColor = Color4::Black();
	color.createUAV = true;
	desc.colors.emplace_back(color);

	return desc;
}

Engine::MultiRenderTargetCreateDesc Engine::RenderPathResources::BuildScreenSpaceOutlineMaskDesc(
	uint32_t width, uint32_t height, std::string_view name, bool createUAV) {

	MultiRenderTargetCreateDesc desc{};
	desc.width = width;
	desc.height = height;

	// Style IDを整数値のまま保持し0はoutlineなしとして毎回clearする
	ColorAttachmentDesc color{};
	color.name = std::string(name);
	color.format = DXGI_FORMAT_R16_UINT;
	color.clearColor = Color4::Black();
	color.createUAV = createUAV;
	desc.colors.emplace_back(color);

	return desc;
}
