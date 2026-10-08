#include "JointAttachmentSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Scene/Utility/SceneEntityKey.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>

// c++
#include <unordered_map>

//============================================================================
//	JointAttachmentSystem classMethods
//============================================================================
void Engine::JointAttachmentSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	changedTransforms_.clear();
	ComponentChangeChannel changedChannels = ComponentChangeChannel::None;
	bool updated = false;

	// Joint接続がなければ追従処理を省く
	bool hasAnyAttachment = false;
	world.ForEach<JointAttachmentComponent>([&](Entity, JointAttachmentComponent&) { hasAnyAttachment = true; });
	if (!hasAnyAttachment) {
		return;
	}

	// Sceneと文書内IDからスキンメッシュを索引化する
	std::unordered_map<SceneEntityKey, Entity, SceneEntityKeyHash> skinnedByLocal;
	world.ForEach<SkinnedAnimationComponent, SceneObjectComponent>(
		[&](Entity entity, SkinnedAnimationComponent&, SceneObjectComponent& sceneObject) {
			skinnedByLocal[{sceneObject.sceneInstanceID, sceneObject.localFileID}] = entity;
		});
	if (skinnedByLocal.empty()) {
		return;
	}

	// 階層の有効状態は既存の伝播処理を使う
	HierarchySystem hierarchySystem{};

	// 親子付けされたエンティティを、ジョイントのワールド行列へ追従させる
	world.ForEach<JointAttachmentComponent, TransformComponent, SceneObjectComponent>(
		[&](Entity entity, JointAttachmentComponent& attachment, TransformComponent& transform,
			SceneObjectComponent& sceneObject) {
			if (attachment.jointName.empty() || !attachment.skinnedEntityLocalFileID) {
				return;
			}
			// 親スキンメッシュエンティティを解決する
			auto skinnedIt =
				skinnedByLocal.find(SceneEntityKey{sceneObject.sceneInstanceID, attachment.skinnedEntityLocalFileID});
			if (skinnedIt == skinnedByLocal.end()) {
				return;
			}
			const Entity skinned = skinnedIt->second;
			if (!world.IsAlive(skinned) || !world.HasComponent<TransformComponent>(skinned)) {
				return;
			}

			// スキンメッシュの有効状態を接続先と子孫へ伝える
			const bool skinnedActive = IsEntityActiveInHierarchy(world, skinned);
			hierarchySystem.RefreshActiveRecursive(world, entity, skinnedActive);
			if (!sceneObject.activeInHierarchy) {
				return;
			}

			const SkinnedAnimationRuntimeData* runtime = TryGetSkinnedAnimationRuntime(world, skinned);
			if (!runtime) {
				return;
			}

			// ジョイントを名前で解決する
			const Skeleton& skeleton = runtime->skeleton;
			const int32_t jointIndex = FindSkeletonJointIndex(skeleton, attachment.jointName);
			if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(skeleton.joints.size())) {
				return;
			}

			// 骨格空間のJoint行列をワールド空間へ変換する
			const Matrix4x4& jointSkeletonSpace = skeleton.joints[jointIndex].skeletonSpaceMatrix;
			const Matrix4x4& skinnedWorld = world.GetComponent<TransformComponent>(skinned).worldMatrix;
			const Matrix4x4 jointWorld = jointSkeletonSpace * skinnedWorld;

			// 親の回転・スケール設定を反映してJointへ追従する
			const Matrix4x4 followJoint =
				BuildParentFollowMatrix(jointWorld, transform.ignoreParentScale, transform.ignoreParentRotation);
			updated |= UpdateWorldMatrix(world, entity, transform, MakeLocalMatrix(transform) * followJoint, changedChannels);

			// 親が毎フレーム動くので、サブツリーを強制的に再計算する
			stack_.clear();
			stack_.emplace_back(entity);
			while (!stack_.empty()) {

				const Entity parent = stack_.back();
				stack_.pop_back();
				if (!world.HasComponent<HierarchyComponent>(parent)) {
					continue;
				}
				const Matrix4x4& parentWorld = world.GetComponent<TransformComponent>(parent).worldMatrix;
				Entity child = world.GetComponent<HierarchyComponent>(parent).firstChild;
				while (child.IsValid()) {

					if (!world.IsAlive(child) || !world.HasComponent<HierarchyComponent>(child)) {
						break;
					}
					auto& childHierarchy = world.GetComponent<HierarchyComponent>(child);
					const Entity nextSibling = childHierarchy.nextSibling;
					if (!IsEntityActiveInHierarchy(world, child)) {
						child = nextSibling;
						continue;
					}
					// 通常のTransform更新と同じく変換のない枝を除く
					auto* childTransform = world.TryGetComponent<TransformComponent>(child);
					if (!childTransform) {
						child = nextSibling;
						continue;
					}
					const Matrix4x4 followParent = BuildParentFollowMatrix(
						parentWorld, childTransform->ignoreParentScale, childTransform->ignoreParentRotation);
					updated |= UpdateWorldMatrix(
						world, child, *childTransform, MakeLocalMatrix(*childTransform) * followParent, changedChannels);
					stack_.emplace_back(child);
					child = nextSibling;
				}
			}
		});
	if (updated) {
		// 走査後に更新した変換をまとめて通知する
		world.MarkDataModified();
		world.MarkTransformConsumersModified(changedChannels, changedTransforms_);
	}
}

bool Engine::JointAttachmentSystem::UpdateWorldMatrix(ECSWorld& world, Entity entity, TransformComponent& transform,
	const Matrix4x4& matrix, ComponentChangeChannel& changedChannels) {

	if (transform.worldMatrix == matrix) {
		transform.isDirty = false;
		return false;
	}
	// 描画と照明へ渡す対象を更新前に確保する
	const ComponentChangeChannel channels = world.GetTransformChangeChannels(entity);
	if (channels != ComponentChangeChannel::None) {
		changedTransforms_.emplace_back(entity);
	}
	transform.worldMatrix = matrix;
	transform.isDirty = false;
	changedChannels |= channels;
	return true;
}
