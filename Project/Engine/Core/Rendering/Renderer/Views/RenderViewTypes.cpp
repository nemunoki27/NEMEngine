#include "RenderViewTypes.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>

//============================================================================
//	RenderViewTypes classMethods
//============================================================================
const Engine::ResolvedCameraView* Engine::ResolvedRenderView::FindCamera(RenderCameraDomain domain) const {

	switch (domain) {
	case RenderCameraDomain::Orthographic:
		return orthographic.valid ? &orthographic : nullptr;
	case RenderCameraDomain::Perspective:
		return perspective.valid ? &perspective : nullptr;
	case RenderCameraDomain::Screen:
		// GameViewでは画面固定、SceneViewでは編集用2DカメラからUIを確認する
		if (kind == RenderViewKind::Scene) {
			return orthographic.valid ? &orthographic : nullptr;
		}
		return screen.valid ? &screen : nullptr;
	}
	return nullptr;
}

std::string Engine::ResolvedRenderView::GetHistoryKey() const {

	const ResolvedCameraView* camera = FindCamera(RenderCameraDomain::Perspective);
	if (!camera) {
		camera = FindCamera(RenderCameraDomain::Orthographic);
	}
	const Entity entity = camera ? camera->sourceCamera : Entity::Null();
	// 同じEntity番号でもWorldと世代が異なれば共有しない
	return std::to_string(historyWorldRevision) + "_" + std::to_string(static_cast<uint32_t>(kind)) + "_" +
		std::to_string(entity.index) + "_" + std::to_string(entity.generation) + "_" + ToString(targetTexture);
}

uint32_t Engine::ResolvedRenderView::GetCullingMask(RenderCameraDomain domain) const {

	const ResolvedCameraView* camera = FindCamera(domain);
	if (!camera) {
		return 0;
	}
	if (kind != RenderViewKind::Game) {
		return camera->cullingMask;
	}
	// 分割画面では2Dと画面UIにも出力Cameraのマスクを使う
	const ResolvedCameraView* outputCamera = FindCamera(RenderCameraDomain::Perspective);
	if (!outputCamera) {
		outputCamera = FindCamera(RenderCameraDomain::Orthographic);
	}
	return outputCamera ? camera->cullingMask & outputCamera->cullingMask : camera->cullingMask;
}
