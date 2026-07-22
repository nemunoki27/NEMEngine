#include "JointAttachmentUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>

//============================================================================
//	JointAttachmentUtility classMethods
//============================================================================
bool Engine::JointAttachmentUtility::ResolveAttachedJoint(ECSWorld& world, const Entity& entity,
	Entity& outSkinnedEntity, Matrix4x4& outSkeletonSpaceMatrix) {

	if (!world.IsAlive(entity) || !world.HasComponent<JointAttachmentComponent>(entity)) {
		return false;
	}
	const auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
	if (attachment.jointName.empty() || !attachment.skinnedEntityLocalFileID) {
		return false;
	}

	// 同じシーン内のlocalFileIDからスキンメッシュエンティティを解決する
	const UUID sceneInstanceID = SceneObjectUtility::GetSceneInstanceID(world, entity);
	outSkinnedEntity = SceneObjectUtility::FindByLocalFileID(
		world, sceneInstanceID, attachment.skinnedEntityLocalFileID);
	if (!world.IsAlive(outSkinnedEntity) || !world.HasComponent<SkinnedAnimationComponent>(outSkinnedEntity)) {
		return false;
	}

	const auto& anim = world.GetComponent<SkinnedAnimationComponent>(outSkinnedEntity);
	const int32_t jointIndex =
		FindSkeletonJointIndex(anim.runtimeSkeleton, attachment.jointName);
	if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(anim.runtimeSkeleton.joints.size())) {
		return false;
	}
	outSkeletonSpaceMatrix = anim.runtimeSkeleton.joints[jointIndex].skeletonSpaceMatrix;
	return true;
}

bool Engine::JointAttachmentUtility::GetJointWorldMatrix(ECSWorld& world, const Entity& skinnedEntity,
	const std::string& jointName, Matrix4x4& outWorldMatrix) {

	if (!world.IsAlive(skinnedEntity) || !world.HasComponent<SkinnedAnimationComponent>(skinnedEntity) ||
		!world.HasComponent<TransformComponent>(skinnedEntity)) {
		return false;
	}
	const auto& anim = world.GetComponent<SkinnedAnimationComponent>(skinnedEntity);
	const int32_t jointIndex = FindSkeletonJointIndex(anim.runtimeSkeleton, jointName);
	if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(anim.runtimeSkeleton.joints.size())) {
		return false;
	}
	const Matrix4x4& jointSkeletonSpace = anim.runtimeSkeleton.joints[jointIndex].skeletonSpaceMatrix;
	const Matrix4x4& skinnedWorld = world.GetComponent<TransformComponent>(skinnedEntity).worldMatrix;
	outWorldMatrix = jointSkeletonSpace * skinnedWorld;
	return true;
}

bool Engine::JointAttachmentUtility::GetAttachedJointWorldMatrix(ECSWorld& world, const Entity& entity,
	Matrix4x4& outWorldMatrix) {

	Entity skinnedEntity = Entity::Null();
	Matrix4x4 jointSkeletonSpace{};
	if (!ResolveAttachedJoint(world, entity, skinnedEntity, jointSkeletonSpace) ||
		!world.HasComponent<TransformComponent>(skinnedEntity)) {
		return false;
	}
	const Matrix4x4& skinnedWorld = world.GetComponent<TransformComponent>(skinnedEntity).worldMatrix;
	outWorldMatrix = jointSkeletonSpace * skinnedWorld;
	return true;
}
