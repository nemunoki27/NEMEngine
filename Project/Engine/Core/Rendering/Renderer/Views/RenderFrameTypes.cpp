#include "RenderFrameTypes.h"

//============================================================================
//	RenderFrameTypes classMethods
//============================================================================

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