#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

namespace Engine {

	namespace {

		// 親階層を辿ってworld回転を組み立てる、worldMatrixの分解ではなくlocal値の積で厳密に求める
		Quaternion ComputeWorldRotation(ECSWorld& world, const Entity& entity) {

			TransformComponent* self = world.TryGetComponent<TransformComponent>(entity);
			Quaternion rotation = self ? self->localRotation : Quaternion::Identity();

			Entity current = entity;
			for (int32_t guard = 0; guard < 1024; ++guard) {
				HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(current);
				if (!hierarchy || !world.IsAlive(hierarchy->parent)) {
					break;
				}
				const Entity parent = hierarchy->parent;
				if (TransformComponent* parentTransform = world.TryGetComponent<TransformComponent>(parent)) {
					rotation = parentTransform->localRotation * rotation;
				}
				current = parent;
			}
			return rotation;
		}

		// 親階層のlocalScaleを成分積で累積したworldのlossy scale、回転による剪断は無視するUnityのlossyScale相当
		Vector3 ComputeWorldScale(ECSWorld& world, const Entity& entity) {

			TransformComponent* self = world.TryGetComponent<TransformComponent>(entity);
			Vector3 scale = self ? self->localScale : Vector3::AnyInit(1.0f);

			Entity current = entity;
			for (int32_t guard = 0; guard < 1024; ++guard) {
				HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(current);
				if (!hierarchy || !world.IsAlive(hierarchy->parent)) {
					break;
				}
				const Entity parent = hierarchy->parent;
				if (TransformComponent* parentTransform = world.TryGetComponent<TransformComponent>(parent)) {
					const Vector3& parentScale = parentTransform->localScale;
					scale = Vector3(scale.x * parentScale.x, scale.y * parentScale.y, scale.z * parentScale.z);
				}
				current = parent;
			}
			return scale;
		}
	}

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
		// ワールド行列の平行移動成分つまりワールド座標を抽出して返す
		return transform ? ToManagedVector3(transform->worldMatrix.GetTranslationValue()) : ManagedVector3{};
	}

	void ManagedScriptRuntime::SetPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		// 予約直後でまだmaterializeしていないEntityはTransformが無いためstagingしflushで付与後に適用する
		if (world->GetCommandBuffer().StageCreatePosition(resolved, ToVector3(value))) {
			return;
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
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

		if (world->GetCommandBuffer().StageCreatePosition(resolved, ToVector3(value))) {
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
		// 親からの相対スケールを返す、デフォルトは等倍の1.0
		return transform ? ToManagedVector3(transform->localScale) : ManagedVector3{ 1.0f, 1.0f, 1.0f };
	}

	void ManagedScriptRuntime::SetLocalScaleCallback(ManagedNativeEntity entity, ManagedVector3 value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		if (world->GetCommandBuffer().StageCreateScale(resolved, ToVector3(value))) {
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
		// 親からの相対回転をクォータニオンで返す
		return transform ? ToManagedQuaternion(transform->localRotation) : ManagedQuaternion{};
	}

	void ManagedScriptRuntime::SetLocalRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}

		if (world->GetCommandBuffer().StageCreateRotation(resolved, Quaternion::Normalize(ToQuaternion(value)))) {
			return;
		}

		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
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
		if (!world || !world->TryGetComponent<TransformComponent>(resolved)) {
			return {};
		}
		// 親階層を含めたworld回転を返す
		return ToManagedQuaternion(ComputeWorldRotation(*world, resolved));
	}

	void ManagedScriptRuntime::SetRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return;
		}
		if (world->GetCommandBuffer().StageCreateRotation(resolved, Quaternion::Normalize(ToQuaternion(value)))) {
			return;
		}
		TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved);
		if (!transform) {
			return;
		}

		// world回転を親のworld回転で打ち消してlocal回転へ変換する、localはinverse parentWorldとworldの積
		Quaternion parentWorld = Quaternion::Identity();
		if (HierarchyComponent* hierarchy = world->TryGetComponent<HierarchyComponent>(resolved)) {
			if (world->IsAlive(hierarchy->parent)) {
				parentWorld = ComputeWorldRotation(*world, hierarchy->parent);
			}
		}
		transform->localRotation = Quaternion::Normalize(Quaternion::Inverse(parentWorld) * ToQuaternion(value));
		MarkDirty(*world, resolved);
	}

	ManagedVector3 ManagedScriptRuntime::GetLossyScaleCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->TryGetComponent<TransformComponent>(resolved)) {
			return ManagedVector3{ 1.0f, 1.0f, 1.0f };
		}
		return ToManagedVector3(ComputeWorldScale(*world, resolved));
	}

} // Engine
