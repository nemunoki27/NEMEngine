#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

namespace Engine {

	//============================================================================
	//	Transform Callbacks
	//	C#側のTransformクラスから呼び出されるネイティブ実装
	//============================================================================

	ManagedVector3 ManagedScriptRuntime::GetPositionCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		// ワールドが無効ならゼロベクトルを返す
		if (!world) {
			return {};
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		// ワールド行列の平行移動成分（ワールド座標）を抽出して返す
		return transform ? ToManagedVector3(transform->worldMatrix.GetTranslationValue()) : ManagedVector3{};
	}

	void ManagedScriptRuntime::SetPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		if (!transform) {
			return;
		}

		// ワールド座標をローカル座標に逆変換してセット。親子階層を考慮した正しい配置を実現
		transform->localPos = MakeLocalPositionFromWorld(*world, resolved, ToVector3(value));
		// トランスフォーム変更を通知し、次フレームの行列再計算を促す
		MarkDirty(*world, resolved);
	}

	ManagedVector3 ManagedScriptRuntime::GetLocalPositionCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return {};
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		// 親からの相対座標をそのまま返す
		return transform ? ToManagedVector3(transform->localPos) : ManagedVector3{};
	}

	void ManagedScriptRuntime::SetLocalPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		if (!transform) {
			return;
		}

		// 親からの相対座標を直接セット
		transform->localPos = ToVector3(value);
		MarkDirty(*world, resolved);
	}

	ManagedVector3 ManagedScriptRuntime::GetLocalScaleCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return ManagedVector3{ 1.0f, 1.0f, 1.0f };
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		// 親からの相対スケールを返す。デフォルトは等倍(1.0)
		return transform ? ToManagedVector3(transform->localScale) : ManagedVector3{ 1.0f, 1.0f, 1.0f };
	}

	void ManagedScriptRuntime::SetLocalScaleCallback(ManagedNativeEntity entity, ManagedVector3 value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		if (!transform) {
			return;
		}

		// 親からの相対スケールをセット
		transform->localScale = ToVector3(value);
		MarkDirty(*world, resolved);
	}

	ManagedQuaternion ManagedScriptRuntime::GetLocalRotationCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return {};
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		// 親からの相対回転（クォータニオン）を返す
		return transform ? ToManagedQuaternion(transform->localRotation) : ManagedQuaternion{};
	}

	void ManagedScriptRuntime::SetLocalRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		if (!transform) {
			return;
		}

		// 親からの相対回転をセット。正規化して数値誤差を補正
		transform->localRotation = Quaternion::Normalize(ToQuaternion(value));
		MarkDirty(*world, resolved);
	}

} // Engine
