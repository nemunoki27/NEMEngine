#include "BuiltinAnimationPropertyGroups.h"

//============================================================================
//	include
//============================================================================
#include "BuiltinAnimationPropertyUtility.h"
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

using namespace Engine::AnimationPropertyUtility;

namespace {

	// 位置を取得する
	bool GetTransformLocalPos(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = transform->localPos;
			return true;
		}
		return false;
	}

	// 位置を変更して子階層へ通知する
	bool SetTransformLocalPos(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector3 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			transform->localPos = typed;
			// 親子階層を持つ場合、子のworldMatrixも再計算対象にする必要がある
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	// 位置のXYを取得する
	bool GetTransformLocalPos2D(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = Engine::Vector2(transform->localPos.x, transform->localPos.y);
			return true;
		}
		return false;
	}

	// Zを保って位置のXYを変更する
	bool SetTransformLocalPos2D(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector2 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			// 2D編集ではZを保持したままXYだけをAnimationClipから上書きする
			transform->localPos.x = typed.x;
			transform->localPos.y = typed.y;
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	// 回転を取得する
	bool GetTransformLocalRotation(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = transform->localRotation;
			return true;
		}
		return false;
	}

	// 回転を正規化して子階層へ通知する
	bool SetTransformLocalRotation(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Quaternion typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			transform->localRotation = Engine::Quaternion::Normalize(typed);
			// Quaternionは4ch保存だが、適用時は正規化して行列生成の誤差を抑える
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	// Z軸の回転角を取得する
	bool GetTransformLocalRotationZ(
		Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = Engine::Quaternion::ToEulerDegrees(transform->localRotation).z;
			return true;
		}
		return false;
	}

	// XYの回転を保ってZ軸を変更する
	bool SetTransformLocalRotationZ(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		float typed = 0.0f;
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			// 2D編集用に、既存のX/Y回転は残してZ回転だけ差し替える
			Engine::Vector3 euler = Engine::Quaternion::ToEulerDegrees(transform->localRotation);
			euler.z = typed;
			transform->localRotation = Engine::Quaternion::FromEulerDegrees(euler);
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	// 拡大率を取得する
	bool GetTransformLocalScale(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = transform->localScale;
			return true;
		}
		return false;
	}

	// 拡大率を変更して子階層へ通知する
	bool SetTransformLocalScale(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector3 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			transform->localScale = typed;
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	// 拡大率のXYを取得する
	bool GetTransformLocalScale2D(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = Engine::Vector2(transform->localScale.x, transform->localScale.y);
			return true;
		}
		return false;
	}

	// Zを保って拡大率のXYを変更する
	bool SetTransformLocalScale2D(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector2 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			// 2D編集ではZ Scaleを保持して、Sprite系の見た目に必要なXYだけ動かす
			transform->localScale.x = typed.x;
			transform->localScale.y = typed.y;
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}
} // namespace

// Transformの編集値を登録する
void Engine::RegisterTransformAnimationProperties(AnimationPropertyRegistry& registry) {

	Register(registry, "Transform", "localPos", "Transform.localPos", AnimationValueType::Vector3,
		HasComponent<TransformComponent>, GetTransformLocalPos, SetTransformLocalPos);
	Register(registry, "Transform", "localPos2D", "Transform.localPos2D", AnimationValueType::Vector2,
		HasComponent<TransformComponent>, GetTransformLocalPos2D, SetTransformLocalPos2D);
	Register(registry, "Transform", "localRotation", "Transform.localRotation", AnimationValueType::Quaternion,
		HasComponent<TransformComponent>, GetTransformLocalRotation, SetTransformLocalRotation);
	Register(registry, "Transform", "localRotationZ", "Transform.localRotationZ", AnimationValueType::Float,
		HasComponent<TransformComponent>, GetTransformLocalRotationZ, SetTransformLocalRotationZ);
	Register(registry, "Transform", "localScale", "Transform.localScale", AnimationValueType::Vector3,
		HasComponent<TransformComponent>, GetTransformLocalScale, SetTransformLocalScale);
	Register(registry, "Transform", "localScale2D", "Transform.localScale2D", AnimationValueType::Vector2,
		HasComponent<TransformComponent>, GetTransformLocalScale2D, SetTransformLocalScale2D);
}
