#include "CameraViewSelection.h"

//============================================================================
//	include
//============================================================================
#include "CameraViewProjection.h"
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

using namespace Engine::CameraViewProjection;

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

		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
		if (!sceneObject) {
			return true;
		}
		return sceneObject->activeInHierarchy;
	}
}

namespace Engine::CameraViewSelection {

	Engine::ResolvedCameraView ResolveBestOrthographicCamera( ECSWorld& world, uint32_t width, uint32_t height) {

		// ワールド内の全てのOrthographicCameraComponentを持つエンティティを走査して、最も優れたカメラ候補を選ぶ
		CameraCandidate<OrthographicCameraComponent> best{};
		world.ForEach<TransformComponent, OrthographicCameraComponent>(
			[&](Entity entity, TransformComponent& transform, OrthographicCameraComponent& camera) {

				if (!camera.common.enabled || !IsActiveCameraEntity(world, entity)) {
					return;
				}

				// トランスフォームとビューポートからカメラ行列を更新する
				UpdateOrthographicCameraMatrices(transform, camera, width, height);

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

	Engine::ResolvedCameraView ResolveBestPerspectiveCamera( ECSWorld& world, uint32_t width, uint32_t height) {

		// ワールド内の全てのPerspectiveCameraComponentを持つエンティティを走査して、最も優れたカメラ候補を選ぶ
		CameraCandidate<PerspectiveCameraComponent> best{};
		world.ForEach<TransformComponent, PerspectiveCameraComponent>(
			[&](Entity entity, TransformComponent& transform, PerspectiveCameraComponent& camera) {

				if (!camera.common.enabled || !IsActiveCameraEntity(world, entity)) {
					return;
				}

				// トランスフォームとビューポートからカメラ行列を更新する
				UpdatePerspectiveCameraMatrices(transform, camera, width, height);

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

	Engine::ResolvedCameraView ResolvePreferredOrthographicCamera(
		ECSWorld& world, UUID preferredCameraUUID, uint32_t width, uint32_t height) {

		if (!preferredCameraUUID) {
			return {};
		}

		Entity entity = world.FindByUUID(preferredCameraUUID);
		auto* transform = world.TryGetComponent<TransformComponent>(entity);
		auto* camera = world.TryGetComponent<OrthographicCameraComponent>(entity);
		if (!transform || !camera || !IsActiveCameraEntity(world, entity)) {
			return {};
		}

		if (!camera->common.enabled) {
			return {};
		}
		// Best経路と同様に現在のトランスフォームとビューポートから行列を作り直す、これが無いと移動が反映されない
		UpdateOrthographicCameraMatrices(*transform, *camera, width, height);
		return BuildFromOrthographicCamera(entity, *transform, *camera);
	}

	Engine::ResolvedCameraView ResolvePreferredPerspectiveCamera(
		ECSWorld& world, UUID preferredCameraUUID, uint32_t width, uint32_t height) {

		if (!preferredCameraUUID) {
			return {};
		}

		Entity entity = world.FindByUUID(preferredCameraUUID);
		auto* transform = world.TryGetComponent<TransformComponent>(entity);
		auto* camera = world.TryGetComponent<PerspectiveCameraComponent>(entity);
		if (!transform || !camera || !IsActiveCameraEntity(world, entity)) {
			return {};
		}

		if (!camera->common.enabled) {
			return {};
		}
		// Best経路と同様に現在のトランスフォームとビューポートから行列を作り直す、これが無いと移動が反映されない
		UpdatePerspectiveCameraMatrices(*transform, *camera, width, height);
		return BuildFromPerspectiveCamera(entity, *transform, *camera);
	}
}
