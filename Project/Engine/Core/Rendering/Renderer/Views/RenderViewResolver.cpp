#include "RenderViewResolver.h"

//============================================================================
//	include
//============================================================================
#include "CameraViewProjection.h"
#include "CameraViewSelection.h"
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

using namespace Engine::CameraViewProjection;
using namespace Engine::CameraViewSelection;

//============================================================================
//	RenderViewResolver classMethods
//============================================================================

Engine::ResolvedRenderView Engine::RenderViewResolver::Resolve(
	const RenderViewRequest& request, ECSWorld& world) {

	// 描画ビューの基本情報を構築する
	ResolvedRenderView view{};
	view.kind = request.kind;
	view.width = request.width;
	view.height = request.height;
	// 無効なビューや0サイズのビューではカメラ行列と描画リソースを構築しない
	if (!request.enabled || request.width == 0 || request.height == 0) {
		return view;
	}
	view.aspectRatio = static_cast<float>(request.width) / static_cast<float>(request.height);
	// 描画ビューの情報を確定させる
	switch (request.sourceKind) {
	case RenderViewSourceKind::WorldCamera:

		return ResolveWorldCameraView(request.kind, world, request.width, request.height,
			request.preferredOrthographicCameraUUID, request.preferredPerspectiveCameraUUID);
	case RenderViewSourceKind::ManualCamera:

		return BuildFromManualCamera(request.kind, request.manualCamera, request.width, request.height);
	}
	return view;
}

Engine::ResolvedRenderView Engine::RenderViewResolver::ResolveWorldCameraView(RenderViewKind kind,
	ECSWorld& world, uint32_t width, uint32_t height,
	UUID preferredOrthographicCameraUUID, UUID preferredPerspectiveCameraUUID) {

	// ワールド内のカメラから描画ビューを確定させる
	ResolvedRenderView view{};
	view.kind = kind;
	view.width = width;
	view.height = height;
	view.aspectRatio = static_cast<float>(width) / static_cast<float>((std::max)(height, 1u));

	// 指定カメラを優先
	if (preferredOrthographicCameraUUID) {
		view.orthographic = ResolvePreferredOrthographicCamera(
			world, preferredOrthographicCameraUUID, width, height);
	}
	if (preferredPerspectiveCameraUUID) {
		view.perspective = ResolvePreferredPerspectiveCamera(
			world, preferredPerspectiveCameraUUID, width, height);
	}
	// 指定カメラが無効な場合はワールド内の全てのカメラから最適なものを選ぶ
	if (!view.orthographic.valid) {

		view.orthographic = ResolveBestOrthographicCamera(world, width, height);
	}
	if (!view.perspective.valid) {

		view.perspective = ResolveBestPerspectiveCamera(world, width, height);
	}
	view.screen = BuildScreenCamera(width, height);
	// ワールド内のカメラが一つも有効でない場合は、マニュアルカメラから描画ビューを構築する
	if (!view.orthographic.valid && !view.perspective.valid) {

		ManualRenderCameraState fallback{};
		view.orthographic = BuildManualOrthographic(fallback, width, height);
		view.perspective = BuildManualPerspective(fallback, width, height);
	}
	view.valid = view.orthographic.valid || view.perspective.valid || view.screen.valid;
	return view;
}

Engine::ResolvedRenderView Engine::RenderViewResolver::BuildFromManualCamera(
	RenderViewKind kind, const ManualRenderCameraState& state, uint32_t width, uint32_t height) {

	// 手動で指定されたカメラから描画ビューを確定させる
	ResolvedRenderView view{};
	view.kind = kind;
	view.width = width;
	view.height = height;
	view.aspectRatio = static_cast<float>(width) / static_cast<float>((std::max)(height, 1u));

	// 2D/3D両方のマニュアルカメラを構築する
	view.orthographic = BuildManualOrthographic(state, width, height);
	view.perspective = BuildManualPerspective(state, width, height);
	view.screen = BuildScreenCamera(width, height);
	view.valid = view.orthographic.valid || view.perspective.valid || view.screen.valid;
	return view;
}
