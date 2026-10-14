#include "RenderFrameTypes.h"

//============================================================================
//	RenderFrameTypes classMethods
//============================================================================
bool Engine::RenderViewRequest::ShouldRender(const ResolvedRenderView& view) const {

	// 画面を隠してもRenderTextureへの出力は継続する
	return enabled && renderThisFrame && view.valid && view.kind == kind &&
		(view.targetTexture || renderDefaultSurfaceThisFrame);
}

const Engine::RenderViewRequest* Engine::RenderFrameRequest::FindView(RenderViewKind kind) const {

	for (const auto& view : views) {
		if (view.kind == kind) {

			return &view;
		}
	}
	return nullptr;
}

Engine::RenderViewRequest* Engine::RenderFrameRequest::FindView(RenderViewKind kind) {

	for (auto& view : views) {
		if (view.kind == kind) {

			return &view;
		}
	}
	return nullptr;
}
