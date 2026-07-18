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
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>

// c++
#include <unordered_map>

//============================================================================
//	JointAttachmentSystem classMethods
//============================================================================
void Engine::JointAttachmentSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	// 親子付けされたエンティティが無ければジョイントの追従計算自体を行わない
	bool hasAnyAttachment = false;
	world.ForEach<JointAttachmentComponent>([&](Entity, JointAttachmentComponent&) {
		hasAnyAttachment = true;
		});
	if (!hasAnyAttachment) {
		return;
	}

	// スキンメッシュエンティティをシーンローカルIDから引けるようにする、参照解決を高速化する
	std::unordered_map<UUID, Entity> skinnedByLocal;
	world.ForEach<SkinnedAnimationComponent, SceneObjectComponent>([&](
		Entity entity, SkinnedAnimationComponent&, SceneObjectComponent& sceneObject) {
			skinnedByLocal[sceneObject.localFileID] = entity;
		});
	if (skinnedByLocal.empty()) {
		return;
	}

	// アクティブ状態の伝播に使う、RefreshActiveRecursiveは状態を持たないのでローカル生成でよい
	HierarchySystem hierarchySystem{};

	// 親子付けされたエンティティを、ジョイントのワールド行列へ追従させる
	world.ForEach<JointAttachmentComponent, TransformComponent>([&](
		Entity entity, JointAttachmentComponent& attachment, TransformComponent& transform) {

			if (attachment.jointName.empty() || !attachment.skinnedEntityLocalFileID) {
				return;
			}
			// 親スキンメッシュエンティティを解決する
			auto skinnedIt = skinnedByLocal.find(attachment.skinnedEntityLocalFileID);
			if (skinnedIt == skinnedByLocal.end()) {
				return;
			}
			const Entity skinned = skinnedIt->second;
			if (!world.IsAlive(skinned) || !world.HasComponent<TransformComponent>(skinned)) {
				return;
			}

			// スキンメッシュの非アクティブを親子付けエンティティと子へも伝える、エンティティ親子付けと同じ挙動にする
			const bool skinnedActive = IsEntityActiveInHierarchy(world, skinned);
			hierarchySystem.RefreshActiveRecursive(world, entity, skinnedActive);

			const auto& anim = world.GetComponent<SkinnedAnimationComponent>(skinned);

			// ジョイントを名前で解決する
			const Skeleton& skeleton = anim.runtimeSkeleton;
			const int32_t jointIndex =
				FindSkeletonJointIndex(skeleton, attachment.jointName);
			if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(skeleton.joints.size())) {
				return;
			}

			// ジョイントのワールド行列、スケルトン空間行列にスキンメッシュのワールド行列を掛ける
			const Matrix4x4& jointSkeletonSpace = skeleton.joints[jointIndex].skeletonSpaceMatrix;
			const Matrix4x4& skinnedWorld = world.GetComponent<TransformComponent>(skinned).worldMatrix;
			const Matrix4x4 jointWorld = jointSkeletonSpace * skinnedWorld;

			// エンティティのローカルSRTはジョイントに対する相対値、行ベクトル規約でlocal * 親worldにする
			// ジョイントもエンティティ親と同じく回転スケールの無視フラグを反映する
			const Matrix4x4 followJoint = BuildParentFollowMatrix(jointWorld,
				transform.ignoreParentScale, transform.ignoreParentRotation);
			transform.worldMatrix = MakeLocalMatrix(transform) * followJoint;
			transform.isDirty = false;

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

					auto& childHierarchy = world.GetComponent<HierarchyComponent>(child);
					if (!IsEntityActiveInHierarchy(world, child)) {
						child = childHierarchy.nextSibling;
						continue;
					}
					if (world.HasComponent<TransformComponent>(child)) {

						auto& childTransform = world.GetComponent<TransformComponent>(child);
						const Matrix4x4 followParent = BuildParentFollowMatrix(parentWorld,
							childTransform.ignoreParentScale, childTransform.ignoreParentRotation);
						childTransform.worldMatrix = MakeLocalMatrix(childTransform) * followParent;
						childTransform.isDirty = false;
					}
					stack_.emplace_back(child);
					child = childHierarchy.nextSibling;
				}
			}
		});
}
