#include "ViewportTransformUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>

// c++
#include <cmath>
#include <algorithm>
#include <optional>

namespace Engine::ViewportTransformUtility {

	bool ResolveWorldDelta(ECSWorld& world, Entity entity, const Vector3& deltaPos, const Quaternion& deltaRotation,
		const Vector3& deltaScale, const Vector3& pivot, bool pivotAtCenter, TransformComponent& result) {

		const auto* transform = world.TryGetComponent<TransformComponent>(entity);
		if (!transform) {
			return false;
		}
		// 実効親の逆行列がなければ変更しない
		const Matrix4x4 parent = GetEntityParentWorldMatrix(world, entity);
		Matrix4x4 inverseParent{};
		if (!Matrix4x4::TryInverse(parent, inverseParent)) {
			return false;
		}
		const Matrix4x4 current =
			Matrix4x4::MakeAffineMatrix(transform->localScale, transform->localRotation, transform->localPos) * parent;
		Vector3 position{};
		Quaternion rotation{};
		Vector3 scale{};
		if (!DecomposeAffine3D(current, position, rotation, scale)) {
			return false;
		}

		// 共通差分をWorldの姿勢へ適用する
		if (pivotAtCenter) {
			position = pivot + RotateVectorByQuaternion(deltaRotation, position - pivot) * deltaScale;
		}
		position += deltaPos;
		rotation = Quaternion::Normalize(deltaRotation * rotation);
		scale = scale * deltaScale;
		const Matrix4x4 local = Matrix4x4::MakeAffineMatrix(scale, rotation, position) * inverseParent;

		// 親座標へ戻せた場合だけ結果を公開する
		TransformComponent resolved = *transform;
		if (!DecomposeAffine3D(local, resolved.localPos, resolved.localRotation, resolved.localScale)) {
			return false;
		}
		result = resolved;
		return true;
	}

	Engine::Vector3 RotateVectorByQuaternion(const Engine::Quaternion& q, const Engine::Vector3& v) {

		const Engine::Vector3 axis(q.x, q.y, q.z);
		const Engine::Vector3 t = Engine::Vector3::Cross(axis, v) * 2.0f;
		return v + t * q.w + Engine::Vector3::Cross(axis, t);
	}

	bool Prefers2DGizmo(const Engine::EditorPanelContext& context, Engine::ECSWorld& world, const Engine::Entity& entity) {

		// Transformを持たない編集対象だけ現在のマニュアルカメラ次元へフォールバックする
		const Engine::Dimension fallback =
			context.editorState ? Engine::ResolveSceneViewCameraDimension(context.editorState->sceneViewPickDimension)
								: Engine::Dimension::Type3D;
		return Engine::ResolveEntityDimension(world, entity).value_or(fallback) == Engine::Dimension::Type2D;
	}

	const Engine::ResolvedCameraView* SelectSceneGizmoCamera(const Engine::ResolvedRenderView& view, bool prefer2DTarget) {

		if (prefer2DTarget) {
			if (view.orthographic.valid) {
				return &view.orthographic;
			}
			if (view.perspective.valid) {
				return &view.perspective;
			}
		} else {
			if (view.perspective.valid) {
				return &view.perspective;
			}
			if (view.orthographic.valid) {
				return &view.orthographic;
			}
		}
		return nullptr;
	}

	Engine::Matrix4x4 GetEntityParentWorldMatrix(Engine::ECSWorld& world, const Engine::Entity& entity) {

		using namespace Engine;
		if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
			return Matrix4x4::Identity();
		}
		const auto& transform = world.GetComponent<TransformComponent>(entity);

		// ジョイント親子付け中はジョイントのワールド行列を親に使う、それ以外はエンティティ階層の親を使う
		Matrix4x4 rawParentWorld = Matrix4x4::Identity();
		Matrix4x4 jointWorld{};
		if (world.HasComponent<JointAttachmentComponent>(entity) &&
			JointAttachmentUtility::GetAttachedJointWorldMatrix(world, entity, jointWorld)) {

			rawParentWorld = jointWorld;
		} else if (world.HasComponent<HierarchyComponent>(entity)) {

			const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
			if (world.IsAlive(hierarchy.parent) && world.HasComponent<TransformComponent>(hierarchy.parent)) {
				rawParentWorld = world.GetComponent<TransformComponent>(hierarchy.parent).worldMatrix;
			}
		}
		// 継承設定を反映した実効親ワールドを返し、ギズモのローカル変換をsystemの計算と一致させる
		return BuildParentFollowMatrix(rawParentWorld, transform.ignoreParentScale, transform.ignoreParentRotation);
	}

	float SnapValueToGrid(float value, float grid) {

		return grid > 0.0f ? std::round(value / grid) * grid : value;
	}

	const Engine::GridSnapAxis* SelectSnapAxis(
		const Engine::EntitySnapSettings& settings, Engine::SceneViewManipulatorMode mode, bool use2D) {

		switch (mode) {
		case Engine::SceneViewManipulatorMode::Translate:
			return use2D ? &settings.translate2D : &settings.translate3D;
		case Engine::SceneViewManipulatorMode::Rotate:
			return use2D ? &settings.rotate2D : &settings.rotate3D;
		case Engine::SceneViewManipulatorMode::Scale:
			return use2D ? &settings.scale2D : &settings.scale3D;
		default:
			return nullptr;
		}
	}

	void ApplyAbsoluteSnap(Engine::TransformComponent& transform, Engine::SceneViewManipulatorMode mode, float grid) {

		switch (mode) {
		case Engine::SceneViewManipulatorMode::Translate:
			transform.localPos.x = SnapValueToGrid(transform.localPos.x, grid);
			transform.localPos.y = SnapValueToGrid(transform.localPos.y, grid);
			transform.localPos.z = SnapValueToGrid(transform.localPos.z, grid);
			break;
		case Engine::SceneViewManipulatorMode::Scale:
			transform.localScale.x = SnapValueToGrid(transform.localScale.x, grid);
			transform.localScale.y = SnapValueToGrid(transform.localScale.y, grid);
			transform.localScale.z = SnapValueToGrid(transform.localScale.z, grid);
			break;
		case Engine::SceneViewManipulatorMode::Rotate: {
			// 回転は一度Euler角へ落としてから丸めて戻す
			Engine::Vector3 euler = Engine::Quaternion::ToEulerAngles(transform.localRotation);
			euler.x = SnapValueToGrid(euler.x, grid);
			euler.y = SnapValueToGrid(euler.y, grid);
			euler.z = SnapValueToGrid(euler.z, grid);
			transform.localRotation = Engine::Quaternion::Normalize(Engine::Quaternion::EulerToQuaternion(euler));
			break;
		}
		default:
			break;
		}
	}
}
