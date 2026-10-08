#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

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

		// LateUpdate前でも現在のlocal値と継承設定から正しいワールド座標を返す
		ResolvedWorldTransform worldTransform{};
		return TransformWorldUtility::ResolveWorldTransform(*world, resolved, worldTransform, true) ?
			ToManagedVector3(worldTransform.matrix.GetTranslationValue()) : ManagedVector3{};
	}

	void ManagedScriptRuntime::SetPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		if (!transform) {
			return;
		}

		// ワールド座標をローカル座標に逆変換してセットし親子階層を考慮した正しい配置を実現する
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

		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		// 親からの相対座標をそのまま返す
		return transform ? ToManagedVector3(transform->localPos) : ManagedVector3{};
	}

	void ManagedScriptRuntime::SetLocalPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
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

		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		// 親からの相対スケールを返す、デフォルトは等倍の1.0
		return transform ? ToManagedVector3(transform->localScale) : ManagedVector3{ 1.0f, 1.0f, 1.0f };
	}

	void ManagedScriptRuntime::SetLocalScaleCallback(ManagedNativeEntity entity, ManagedVector3 value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
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

		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		// 親からの相対回転をクォータニオンで返す
		return transform ? ToManagedQuaternion(transform->localRotation) : ManagedQuaternion{};
	}

	void ManagedScriptRuntime::SetLocalRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		if (!transform) {
			return;
		}

		// 親からの相対回転をセットし正規化して数値誤差を補正
		transform->localRotation = Quaternion::Normalize(ToQuaternion(value));
		MarkDirty(*world, resolved);
	}

	ManagedQuaternion ManagedScriptRuntime::GetRotationCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return {};
		}
		// LateUpdate前でも現在のlocal値と継承設定から正しいワールド回転を返す
		ResolvedWorldTransform worldTransform{};
		return TransformWorldUtility::ResolveWorldTransform(*world, resolved, worldTransform, true) ?
			ToManagedQuaternion(worldTransform.rotation) : ManagedQuaternion{};
	}

	void ManagedScriptRuntime::SetRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}
		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		if (!transform) {
			return;
		}

		// 継承設定を反映した親回転で打ち消してワールド回転をlocalへ変換する
		ResolvedWorldTransform parentFollow{};
		if (!TransformWorldUtility::ResolveParentFollowTransform(*world, resolved, parentFollow, true)) {
			return;
		}
		transform->localRotation = Quaternion::Normalize(
			Quaternion::Inverse(parentFollow.rotation) * ToQuaternion(value));
		MarkDirty(*world, resolved);
	}

	ManagedVector3 ManagedScriptRuntime::GetLossyScaleCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return ManagedVector3{ 1.0f, 1.0f, 1.0f };
		}
		ResolvedWorldTransform worldTransform{};
		return TransformWorldUtility::ResolveWorldTransform(*world, resolved, worldTransform, true) ?
			ToManagedVector3(worldTransform.scale) : ManagedVector3{ 1.0f, 1.0f, 1.0f };
	}

	int32_t ManagedScriptRuntime::GetIgnoreParentRotationCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return 0;
		}
		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		return (transform && transform->ignoreParentRotation) ? 1 : 0;
	}

	void ManagedScriptRuntime::SetIgnoreParentRotationCallback(ManagedNativeEntity entity, int32_t value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}
		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		if (!transform) {
			return;
		}
		transform->ignoreParentRotation = value != 0;
		MarkDirty(*world, resolved);
	}

	int32_t ManagedScriptRuntime::GetIgnoreParentScaleCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return 0;
		}
		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		return (transform && transform->ignoreParentScale) ? 1 : 0;
	}

	void ManagedScriptRuntime::SetIgnoreParentScaleCallback(ManagedNativeEntity entity, int32_t value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}
		TransformComponent* transform = world->TryGetComponentForBinding<TransformComponent>(resolved);
		if (!transform) {
			return;
		}
		transform->ignoreParentScale = value != 0;
		MarkDirty(*world, resolved);
	}

} // Engine
