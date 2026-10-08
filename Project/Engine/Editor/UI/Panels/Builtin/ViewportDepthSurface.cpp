#include "ViewportDepthSurface.h"
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthTexture2D.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include "ViewportTransformUtility.h"

// c++
#include <cmath>
#include <algorithm>
#include <optional>


using namespace Engine::ViewportTransformUtility;

const Engine::RenderTexture2D* Engine::ViewportDepthSurface::RenderDepthVisualization(
	const EditorPanelContext& context, RenderViewKind viewKind, uint32_t width, uint32_t height) {

	if (!context.graphicsCore || !context.renderPipeline || width == 0 || height == 0) {
		return nullptr;
	}
	DepthTexture2D* depth = context.renderPipeline->GetViewDepthTexture(viewKind);
	if (!depth) {
		return nullptr;
	}
	const ResolvedRenderView& view =
		context.renderPipeline->GetResolvedView(viewKind);
	const ResolvedCameraView* camera =
		view.FindCamera(RenderCameraDomain::Perspective);
	if (!camera) {
		camera = view.FindCamera(RenderCameraDomain::Orthographic);
	}
	if (!camera) {
		return nullptr;
	}

	// サイズが変わったら可視化サーフェスを作り直す、深度は持たない1色のグレースケール出力
	if (!depthVisualizeSurface_ || depthVisualizeWidth_ != width || depthVisualizeHeight_ != height) {

		depthVisualizeSurface_ = std::make_unique<MultiRenderTarget>();

		MultiRenderTargetCreateDesc desc{};
		desc.width = width;
		desc.height = height;
		ColorAttachmentDesc color{};
		color.name = "GBufferDebug.Depth";
		color.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		color.clearColor = Color4::Black();
		color.createUAV = false;
		desc.colors.emplace_back(color);

		depthVisualizeSurface_->Create(context.graphicsCore->GetDXObject().GetDevice(),
			&context.graphicsCore->GetRTVDescriptor(), &context.graphicsCore->GetDSVDescriptor(),
			&context.graphicsCore->GetSRVDescriptor(), desc);
		depthVisualizeWidth_ = width;
		depthVisualizeHeight_ = height;
	}
	if (!depthVisualizeSurface_->IsValid()) {
		return nullptr;
	}

	// 深度を線形化グレースケールへ変換して可視化サーフェスへ描く
	return depthVisualizer_.Render(
		*context.graphicsCore, *camera, depth, *depthVisualizeSurface_);
}
