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

std::vector<Engine::ResolvedRenderView> Engine::RenderViewResolver::ResolveGameCameraViews(
	const RenderViewRequest& request, ECSWorld& world) {

	if (!request.enabled || request.width == 0 || request.height == 0 ||
		request.sourceKind != RenderViewSourceKind::WorldCamera) {
		return {};
	}

	const ResolvedCameraView orthographic = ResolveBestOrthographicCamera(
		world, request.width, request.height);
	const std::vector<ResolvedPerspectiveCameraOutput> outputs =
		ResolvePerspectiveCameraOutputs(world, request.width, request.height);
	std::vector<ResolvedRenderView> views{};
	views.reserve(outputs.size());
	for (const ResolvedPerspectiveCameraOutput& output : outputs) {

		ResolvedRenderView view{};
		view.kind = request.kind;
		view.targetTexture = output.output.targetTexture;
		view.normalizedOutputX = std::clamp(output.output.viewportX, 0.0f, 1.0f);
		view.normalizedOutputY = std::clamp(output.output.viewportY, 0.0f, 1.0f);
		view.normalizedOutputWidth = std::clamp(output.output.viewportWidth, 0.0f, 1.0f);
		view.normalizedOutputHeight = std::clamp(output.output.viewportHeight, 0.0f, 1.0f);
		view.outputX = static_cast<uint32_t>(std::round(static_cast<float>(request.width) *
			view.normalizedOutputX));
		view.outputY = static_cast<uint32_t>(std::round(static_cast<float>(request.height) *
			view.normalizedOutputY));
		view.outputWidth = (std::max)(1u, static_cast<uint32_t>(std::round(static_cast<float>(request.width) *
			view.normalizedOutputWidth)));
		view.outputHeight = (std::max)(1u, static_cast<uint32_t>(std::round(static_cast<float>(request.height) *
			view.normalizedOutputHeight)));
		view.outputWidth = (std::min)(view.outputWidth, request.width - (std::min)(view.outputX, request.width - 1));
		view.outputHeight = (std::min)(view.outputHeight, request.height - (std::min)(view.outputY, request.height - 1));
		view.width = view.outputWidth;
		view.height = view.outputHeight;
		view.aspectRatio = static_cast<float>(view.width) / static_cast<float>(view.height);
		view.orthographic = orthographic;
		view.perspective = output.camera;
		view.screen = BuildScreenCamera(view.width, view.height);
		view.valid = true;
		views.emplace_back(std::move(view));
	}
	return views;
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
	view.outputWidth = width;
	view.outputHeight = height;

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
	view.outputWidth = width;
	view.outputHeight = height;

	// 2D/3D両方のマニュアルカメラを構築する
	view.orthographic = BuildManualOrthographic(state, width, height);
	view.perspective = BuildManualPerspective(state, width, height);
	view.screen = BuildScreenCamera(width, height);
	view.valid = view.orthographic.valid || view.perspective.valid || view.screen.valid;
	return view;
}
