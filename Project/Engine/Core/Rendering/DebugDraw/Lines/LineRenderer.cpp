#include "LineRenderer.h"

#if defined(_DEBUG) || defined(_DEVELOPBUILD)

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// imgui
#include <imgui.h>

//============================================================================
//	LineRenderer classMethods
//============================================================================
Engine::LineRenderer* Engine::LineRenderer::instance_ = nullptr;

Engine::LineRenderer* Engine::LineRenderer::GetInstance() {

	if (instance_ == nullptr) {
		instance_ = new LineRenderer();
	}
	return instance_;
}

Engine::LineRenderer::~LineRenderer() {

	// 各ライン描画クラスが持つGPUバッファをLeakChecker前に明示resetする
	renderer3D_.reset();
	renderer2D_.reset();
}

void Engine::LineRenderer::Finalize() {

	if (instance_ != nullptr) {

		delete instance_;
		instance_ = nullptr;
	}
}

void Engine::LineRenderer::Init(GraphicsCore& graphicsCore) {

	// 各次元のライン描画クラス初期化
	renderer2D_ = std::make_unique<LineRenderer2D>(graphicsCore, RenderCameraDomain::Orthographic);
	renderer3D_ = std::make_unique<LineRenderer3D>(graphicsCore, RenderCameraDomain::Perspective);
}

void Engine::LineRenderer::BeginFrame() {

	renderer2D_->BeginFrame();
	renderer3D_->BeginFrame();
}

void Engine::LineRenderer::RenderSceneView(GraphicsCore& graphicsCore,
	const ResolvedRenderView& view, MultiRenderTarget& surface, bool drawDefaultGrid, bool drawQueuedLines,
	DepthTexture2D* snapGridOcclusionDepth) {

	if (drawDefaultGrid) {

		// SceneViewのデフォルトグリッドは、SceneView側のカメラで直接描画、こちらは占有させない
		renderer3D_->RenderDefaultGrid(graphicsCore, view, surface);
	}

	// スナップグリッドだけメッシュに隠すためのシーン深度を渡す
	renderer3D_->SetSnapGridOcclusionDepth(snapGridOcclusionDepth);

	// 各次元のライン描画クラスに描画呼び出し
	renderer2D_->RenderSceneView(graphicsCore, view, surface, drawQueuedLines);
	renderer3D_->RenderSceneView(graphicsCore, view, surface, drawQueuedLines);
}
#endif
