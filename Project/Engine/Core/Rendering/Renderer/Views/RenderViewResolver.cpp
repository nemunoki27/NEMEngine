#include "RenderViewResolver.h"

//============================================================================
//	include
//============================================================================
#include "CameraViewProjection.h"
#include "CameraViewSelection.h"
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <cmath>

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
	view.referenceWidth = request.GetReferenceWidth();
	view.referenceHeight = request.GetReferenceHeight();
	// 無効なビューや0サイズのビューではカメラ行列と描画リソースを構築しない
	if (!request.enabled || request.width == 0 || request.height == 0) {
		return view;
	}
	const uint32_t referenceWidth = view.referenceWidth;
	const uint32_t referenceHeight = view.referenceHeight;
	// 描画ビューの情報を確定させる
	switch (request.sourceKind) {
	case RenderViewSourceKind::WorldCamera:

		view = ResolveWorldCameraView(request.kind, world, referenceWidth, referenceHeight,
			request.preferredOrthographicCameraUUID, request.preferredPerspectiveCameraUUID);
		break;
	case RenderViewSourceKind::ManualCamera:

		view = BuildFromManualCamera(request.kind, request.manualCamera, referenceWidth, referenceHeight);
		break;
	}
	// 行列と画面座標は維持し、Rasterの範囲だけを実際の出力サイズへ合わせる
	view.width = view.outputWidth = request.width;
	view.height = view.outputHeight = request.height;
	return view;
}

std::vector<Engine::ResolvedRenderView> Engine::RenderViewResolver::ResolveGameCameraViews(
	const RenderViewRequest& request, ECSWorld& world) {

	if (!request.enabled || request.width == 0 || request.height == 0 ||
		request.sourceKind != RenderViewSourceKind::WorldCamera) {
		return {};
	}

	const uint32_t referenceWidth = request.GetReferenceWidth();
	const uint32_t referenceHeight = request.GetReferenceHeight();
	const ResolvedCameraView orthographic = ResolveBestOrthographicCamera(
		world, referenceWidth, referenceHeight);
	const std::vector<ResolvedPerspectiveCameraOutput> outputs =
		ResolvePerspectiveCameraOutputs(world, referenceWidth, referenceHeight);
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
		const uint32_t referenceX = (std::min)(referenceWidth - 1, static_cast<uint32_t>(
			std::round(static_cast<float>(referenceWidth) * view.normalizedOutputX)));
		const uint32_t referenceY = (std::min)(referenceHeight - 1, static_cast<uint32_t>(
			std::round(static_cast<float>(referenceHeight) * view.normalizedOutputY)));
		view.referenceWidth = (std::min)(referenceWidth - referenceX, (std::max)(1u, static_cast<uint32_t>(
			std::round(static_cast<float>(referenceWidth) * view.normalizedOutputWidth))));
		view.referenceHeight = (std::min)(referenceHeight - referenceY, (std::max)(1u, static_cast<uint32_t>(
			std::round(static_cast<float>(referenceHeight) * view.normalizedOutputHeight))));
		view.aspectRatio = static_cast<float>(view.referenceWidth) / static_cast<float>(view.referenceHeight);
		view.orthographic = orthographic;
		view.perspective = output.camera;
		view.screen = BuildScreenCamera(view.referenceWidth, view.referenceHeight);
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
	view.referenceWidth = width;
	view.referenceHeight = height;
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
	view.referenceWidth = width;
	view.referenceHeight = height;
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

void Engine::RenderViewResolver::ResolveRenderTextureOutput(ResolvedRenderView& view, ECSWorld& world,
	uint32_t width, uint32_t height) {

	if (!width || !height) { view.valid = false; return; }
	view.outputX = static_cast<uint32_t>(std::round(
		static_cast<float>(width) * view.normalizedOutputX));
	view.outputY = static_cast<uint32_t>(std::round(
		static_cast<float>(height) * view.normalizedOutputY));
	view.outputWidth = (std::max)(1u, static_cast<uint32_t>(std::round(
		static_cast<float>(width) * view.normalizedOutputWidth)));
	view.outputHeight = (std::max)(1u, static_cast<uint32_t>(std::round(
		static_cast<float>(height) * view.normalizedOutputHeight)));
	view.outputX = (std::min)(view.outputX, width - 1);
	view.outputY = (std::min)(view.outputY, height - 1);
	view.outputWidth = (std::min)(view.outputWidth, width - view.outputX);
	view.outputHeight = (std::min)(view.outputHeight, height - view.outputY);
	view.width = view.outputWidth;
	view.height = view.outputHeight;
	view.referenceWidth = view.width;
	view.referenceHeight = view.height;
	view.aspectRatio = static_cast<float>(view.width) / static_cast<float>(view.height);
	TransformComponent* transform = world.TryGetComponent<TransformComponent>(
		view.perspective.sourceCamera);
	PerspectiveCameraComponent* camera = world.TryGetComponent<PerspectiveCameraComponent>(
		view.perspective.sourceCamera);
	if (transform && camera) {
		CameraViewProjection::UpdatePerspectiveCameraMatrices(
			*transform, *camera, view.width, view.height);
		view.perspective = CameraViewProjection::BuildFromPerspectiveCamera(
			view.perspective.sourceCamera, *transform, *camera);
	}
	TransformComponent* orthographicTransform =
		world.TryGetComponent<TransformComponent>(
			view.orthographic.sourceCamera);
	OrthographicCameraComponent* orthographicCamera =
		world.TryGetComponent<OrthographicCameraComponent>(
			view.orthographic.sourceCamera);
	if (orthographicTransform && orthographicCamera) {
		CameraViewProjection::UpdateOrthographicCameraMatrices(
			*orthographicTransform, *orthographicCamera,
			view.width, view.height);
		view.orthographic = CameraViewProjection::BuildFromOrthographicCamera(
			view.orthographic.sourceCamera,
			*orthographicTransform, *orthographicCamera);
	}
	view.screen = CameraViewProjection::BuildScreenCamera(view.width, view.height);
}

const Engine::ResolvedRenderView* Engine::RenderViewResolver::FindOutputViewAtPixel(
	std::span<const ResolvedRenderView> views, const Vector2& inputPixel, Vector2& viewPixel) {

	viewPixel = Vector2::AnyInit(0.0f);
	if (!std::isfinite(inputPixel.x) || !std::isfinite(inputPixel.y)) return nullptr;
	for (auto it = views.rbegin(); it != views.rend(); ++it) {
		const ResolvedRenderView& view = *it;
		if (!view.valid || view.targetTexture || view.width == 0 || view.height == 0 ||
			inputPixel.x < static_cast<float>(view.outputX) || inputPixel.y < static_cast<float>(view.outputY) ||
			static_cast<float>(view.outputX + view.outputWidth) <= inputPixel.x ||
			static_cast<float>(view.outputY + view.outputHeight) <= inputPixel.y) continue;
		viewPixel = inputPixel - Vector2(static_cast<float>(view.outputX), static_cast<float>(view.outputY));
		return &view;
	}
	return nullptr;
}
