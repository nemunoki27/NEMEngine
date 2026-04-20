#include "LineRenderer.h"

#if defined(_DEBUG) || defined(_DEVELOPBUILD)

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Logger/Assert.h>

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
	const ResolvedRenderView& view, MultiRenderTarget& surface) {

	// 各次元のライン描画クラスに描画呼び出し
	renderer2D_->RenderSceneView(graphicsCore, view, surface);
	renderer3D_->RenderSceneView(graphicsCore, view, surface);
}
#endif