#include "RenderViewResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/MathLib/Math.h>

//============================================================================
//	RenderViewResolver classMethods
//============================================================================

namespace {

	// 使用するカメラ候補
	template <typename TCamera>
	struct CameraCandidate {

		Engine::Entity entity = Engine::Entity::Null();
		Engine::TransformComponent transform = {};
		TCamera camera = {};

		bool IsValid() const { return entity.IsValid(); }
	};
	// カメラ候補の優劣を比較する関数
	template<class TCamera>
	bool IsBetterCandidate(const CameraCandidate<TCamera>& lhs, const CameraCandidate<TCamera>& rhs) {

		if (!rhs.IsValid()) {
			return true;
		}
		if (lhs.camera.common.isMain != rhs.camera.common.isMain) {
			return lhs.camera.common.isMain && !rhs.camera.common.isMain;
		}
		if (lhs.camera.common.priority != rhs.camera.common.priority) {
			return lhs.camera.common.priority > rhs.camera.common.priority;
		}
		if (lhs.entity.index != rhs.entity.index) {
			return lhs.entity.index < rhs.entity.index;
		}
		return lhs.entity.generation < rhs.entity.generation;
	}
	// カメラエンティティがアクティブかどうか
	static bool IsActiveCameraEntity(Engine::ECSWorld& world, Engine::Entity entity) {

		if (!world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			return true;
		}
		const auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
		return sceneObject.activeInHierarchy;
	}
	// マニュアルカメラのワールド行列を構築する関数
	static Engine::Matrix4x4 BuildManualWorld(const Engine::ManualRenderCameraTransform& transform) {

		return Engine::Matrix4x4::MakeAffineMatrix(Engine::Vector3::AnyInit(1.0f), transform.rotation, transform.pos);
	}
}

Engine::ResolvedRenderView Engine::RenderViewResolver::Resolve(
	const RenderViewRequest& request, ECSWorld& world) {

	// 描画ビューの基本情報を構築する
	ResolvedRenderView view{};
	view.kind = request.kind;
	view.width = request.width;
	view.height = request.height;
	view.aspectRatio = static_cast<float>(request.width) / static_cast<float>(request.height);
	// 無効な場合はここで返す
	if (!request.enabled) {
		return view;
	}
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
			world, preferredOrthographicCameraUUID);
	}
	if (preferredPerspectiveCameraUUID) {
		view.perspective = ResolvePreferredPerspectiveCamera(
			world, preferredPerspectiveCameraUUID);
	}
	// 指定カメラが無効な場合はワールド内の全てのカメラから最適なものを選ぶ
	if (!view.orthographic.valid) {

		view.orthographic = ResolveBestOrthographicCamera(world, width, height);
	}
	if (!view.perspective.valid) {

		view.perspective = ResolveBestPerspectiveCamera(world, width, height);
	}
	// ワールド内のカメラが一つも有効でない場合は、マニュアルカメラから描画ビューを構築する
	if (!view.orthographic.valid && !view.perspective.valid) {

		ManualRenderCameraState fallback{};
		view.orthographic = BuildManualOrthographic(fallback, width, height);
		view.perspective = BuildManualPerspective(fallback, width, height);
	}
	view.valid = view.orthographic.valid || view.perspective.valid;
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
	view.valid = view.orthographic.valid || view.perspective.valid;
	return view;
}

Engine::ResolvedCameraView Engine::RenderViewResolver::ResolveBestOrthographicCamera(
	ECSWorld& world, uint32_t width, uint32_t height) {

	// ワールド内の全てのOrthographicCameraComponentを持つエンティティを走査して、最も優れたカメラ候補を選ぶ
	CameraCandidate<OrthographicCameraComponent> best{};
	world.ForEach<TransformComponent, OrthographicCameraComponent>(
		[&](Entity entity, TransformComponent& transform, OrthographicCameraComponent& camera) {

			if (!camera.common.enabled || !IsActiveCameraEntity(world, entity)) {
				return;
			}

			// アスペクト比を計算
			camera.common.aspectRatio = static_cast<float>(width) / static_cast<float>((std::max)(height, 1u));
			// カメラ行列を更新
			camera.common.viewMatrix = Matrix4x4::Inverse(transform.worldMatrix);
			camera.common.projectionMatrix = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f,
				static_cast<float>(width), static_cast<float>(height), camera.nearClip, camera.farClip);
			camera.common.viewProjectionMatrix = camera.common.viewMatrix * camera.common.projectionMatrix;

			CameraCandidate<OrthographicCameraComponent> candidate{};
			candidate.entity = entity;
			candidate.transform = transform;
			candidate.camera = camera;
			// 優れた候補であれば更新する
			if (IsBetterCandidate(candidate, best)) {
				best = candidate;
			}
		});
	if (!best.IsValid()) {
		return {};
	}
	return BuildFromOrthographicCamera(best.entity, best.transform, best.camera);
}

Engine::ResolvedCameraView Engine::RenderViewResolver::ResolveBestPerspectiveCamera(
	ECSWorld& world, uint32_t width, uint32_t height) {

	// ワールド内の全てのPerspectiveCameraComponentを持つエンティティを走査して、最も優れたカメラ候補を選ぶ
	CameraCandidate<PerspectiveCameraComponent> best{};
	world.ForEach<TransformComponent, PerspectiveCameraComponent>(
		[&](Entity entity, TransformComponent& transform, PerspectiveCameraComponent& camera) {

			if (!camera.common.enabled || !IsActiveCameraEntity(world, entity)) {
				return;
			}

			// アスペクト比を計算
			camera.common.aspectRatio = static_cast<float>(width) / static_cast<float>((std::max)(height, 1u));
			// カメラ行列を更新
			camera.common.viewMatrix = Matrix4x4::Inverse(transform.worldMatrix);
			camera.common.projectionMatrix = Matrix4x4::MakePerspectiveFovMatrix(
				camera.fovY, camera.common.aspectRatio, camera.nearClip, camera.farClip);
			camera.common.viewProjectionMatrix = camera.common.viewMatrix * camera.common.projectionMatrix;

			CameraCandidate<PerspectiveCameraComponent> candidate{};
			candidate.entity = entity;
			candidate.transform = transform;
			candidate.camera = camera;
			// 優れた候補であれば更新する
			if (IsBetterCandidate(candidate, best)) {
				best = candidate;
			}
		});

	if (!best.IsValid()) {
		return {};
	}
	return BuildFromPerspectiveCamera(best.entity, best.transform, best.camera);
}

Engine::ResolvedCameraView Engine::RenderViewResolver::BuildFromOrthographicCamera(
	const Entity& entity, const TransformComponent& transform,
	OrthographicCameraComponent& camera) {

	// カメラコンポーネントから描画ビューの情報を構築する
	ResolvedCameraView out{};
	out.valid = true;
	out.sourceCamera = entity;
	out.usesManualCamera = false;
	out.cameraPos = transform.worldMatrix.GetTranslationValue();
	out.forward = Vector3(transform.worldMatrix.m[2][0], transform.worldMatrix.m[2][1], transform.worldMatrix.m[2][2]).Normalize();
	out.nearClip = camera.nearClip;
	out.farClip = (std::max)(camera.farClip, out.nearClip + 0.001f);
	out.cullingMask = static_cast<uint32_t>(camera.common.cullingMask);

	out.matrices.inverseViewMatrix = transform.worldMatrix;
	out.matrices.viewMatrix = camera.common.viewMatrix;
	out.matrices.projectionMatrix = camera.common.projectionMatrix;
	out.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(out.matrices.projectionMatrix);
	out.matrices.viewProjectionMatrix = camera.common.viewProjectionMatrix;

	return out;
}

Engine::ResolvedCameraView Engine::RenderViewResolver::BuildFromPerspectiveCamera(
	const Entity& entity, const TransformComponent& transform, PerspectiveCameraComponent& camera) {

	// カメラコンポーネントから描画ビューの情報を構築する
	ResolvedCameraView out{};
	out.valid = true;
	out.sourceCamera = entity;
	out.usesManualCamera = false;
	out.cameraPos = transform.worldMatrix.GetTranslationValue();
	out.forward = Vector3(transform.worldMatrix.m[2][0], transform.worldMatrix.m[2][1], transform.worldMatrix.m[2][2]).Normalize();
	out.nearClip = (std::max)(camera.nearClip, 0.001f);
	out.farClip = (std::max)(camera.farClip, out.nearClip + 0.001f);
	out.cullingMask = static_cast<uint32_t>(camera.common.cullingMask);

	out.matrices.inverseViewMatrix = transform.worldMatrix;
	out.matrices.viewMatrix = camera.common.viewMatrix;
	out.matrices.projectionMatrix = camera.common.projectionMatrix;
	out.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(out.matrices.projectionMatrix);
	out.matrices.viewProjectionMatrix = camera.common.viewProjectionMatrix;

	return out;
}

Engine::ResolvedCameraView Engine::RenderViewResolver::BuildManualOrthographic(
	const ManualRenderCameraState& state, uint32_t width, uint32_t height) {

	ResolvedCameraView out{};
	if (!state.enableOrthographic) {
		return out;
	}

	// マニュアルカメラの状態から描画ビューの情報を構築する
	out.valid = true;
	out.usesManualCamera = true;
	out.cameraPos = state.transform2D.pos;
	out.nearClip = state.orthoNearClip;
	out.farClip = (std::max)(state.orthoFarClip, out.nearClip + 0.001f);
	out.cullingMask = static_cast<uint32_t>(state.orthographicCullingMask);

	Matrix4x4 world = BuildManualWorld(state.transform2D);
	out.matrices.inverseViewMatrix = world;
	out.matrices.viewMatrix = Matrix4x4::Inverse(world);
	out.matrices.projectionMatrix = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f,
		static_cast<float>(width), static_cast<float>(height), out.nearClip, out.farClip);
	out.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(out.matrices.projectionMatrix);
	out.matrices.viewProjectionMatrix = out.matrices.viewMatrix * out.matrices.projectionMatrix;

	return out;
}

Engine::ResolvedCameraView Engine::RenderViewResolver::ResolvePreferredOrthographicCamera(ECSWorld& world, UUID preferredCameraUUID) {

	if (!preferredCameraUUID) {
		return {};
	}

	Entity entity = world.FindByUUID(preferredCameraUUID);
	if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity) ||
		!world.HasComponent<OrthographicCameraComponent>(entity) || !IsActiveCameraEntity(world, entity)) {
		return {};
	}

	const auto& transform = world.GetComponent<TransformComponent>(entity);
	auto& camera = world.GetComponent<OrthographicCameraComponent>(entity);
	if (!camera.common.enabled) {
		return {};
	}
	return BuildFromOrthographicCamera(entity, transform, camera);
}

Engine::ResolvedCameraView Engine::RenderViewResolver::BuildManualPerspective(
	const ManualRenderCameraState& state, uint32_t width, uint32_t height) {

	ResolvedCameraView out{};
	if (!state.enablePerspective) {
		return out;
	}

	// マニュアルカメラの状態から描画ビューの情報を構築する
	out.valid = true;
	out.usesManualCamera = true;
	out.cameraPos = state.transform3D.pos;
	out.nearClip = (std::max)(state.perspectiveNearClip, 0.001f);
	out.farClip = (std::max)(state.perspectiveFarClip, out.nearClip + 0.001f);
	out.cullingMask = static_cast<uint32_t>(state.perspectiveCullingMask);

	float aspect = static_cast<float>(width) / static_cast<float>((std::max)(height, 1u));

	Matrix4x4 world = BuildManualWorld(state.transform3D);
	out.matrices.inverseViewMatrix = world;
	out.matrices.viewMatrix = Matrix4x4::Inverse(world);
	out.matrices.projectionMatrix = Matrix4x4::MakePerspectiveFovMatrix(state.perspectiveFovY, aspect, out.nearClip, out.farClip);
	out.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(out.matrices.projectionMatrix);
	out.matrices.viewProjectionMatrix = out.matrices.viewMatrix * out.matrices.projectionMatrix;

	return out;
}

Engine::ResolvedCameraView Engine::RenderViewResolver::ResolvePreferredPerspectiveCamera(ECSWorld& world, UUID preferredCameraUUID) {

	if (!preferredCameraUUID) {
		return {};
	}

	Entity entity = world.FindByUUID(preferredCameraUUID);
	if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity) ||
		!world.HasComponent<PerspectiveCameraComponent>(entity) || !IsActiveCameraEntity(world, entity)) {
		return {};
	}

	const auto& transform = world.GetComponent<TransformComponent>(entity);
	auto& camera = world.GetComponent<PerspectiveCameraComponent>(entity);
	if (!camera.common.enabled) {
		return {};
	}
	return BuildFromPerspectiveCamera(entity, transform, camera);
}