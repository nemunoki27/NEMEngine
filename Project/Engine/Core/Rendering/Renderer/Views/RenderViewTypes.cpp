#include "RenderViewTypes.h"

//============================================================================
//	RenderViewTypes classMethods
//============================================================================

const Engine::ResolvedCameraView* Engine::ResolvedRenderView::FindCamera(RenderCameraDomain domain) const {

	switch (domain) {
	case RenderCameraDomain::Orthographic:
		return orthographic.valid ? &orthographic : nullptr;
	case RenderCameraDomain::Perspective:
		return perspective.valid ? &perspective : nullptr;
	}
	return nullptr;
}