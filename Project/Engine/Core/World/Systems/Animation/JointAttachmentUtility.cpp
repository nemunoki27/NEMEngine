#include "JointAttachmentUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

//============================================================================
//	JointAttachmentUtility classMethods
//============================================================================
bool Engine::JointAttachmentUtility::GetJointWorldMatrix(ECSWorld& world, const Entity& skinnedEntity,
	const std::string& jointName, Matrix4x4& outWorldMatrix) {

	if (!world.IsAlive(skinnedEntity) || !world.HasComponent<SkinnedAnimationComponent>(skinnedEntity) ||
		!world.HasComponent<TransformComponent>(skinnedEntity)) {
		return false;
	}
	const auto& anim = world.GetComponent<SkinnedAnimationComponent>(skinnedEntity);
	auto jointIt = anim.runtimeSkeleton.jointMap.find(jointName);
	if (jointIt == anim.runtimeSkeleton.jointMap.end()) {
		return false;
	}
	const int32_t jointIndex = jointIt->second;
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

	if (!world.IsAlive(entity) || !world.HasComponent<JointAttachmentComponent>(entity)) {
		return false;
	}
	const auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
	if (attachment.jointName.empty() || !attachment.skinnedEntityLocalFileID) {
		return false;
	}
	// localFileIDからスキンメッシュエンティティを解決する、Edit/Playをまたいでも一意に引ける
	const Entity skinnedEntity = SceneObjectUtility::FindByLocalFileID(world, attachment.skinnedEntityLocalFileID);
	if (!world.IsAlive(skinnedEntity)) {
		return false;
	}
	return GetJointWorldMatrix(world, skinnedEntity, attachment.jointName, outWorldMatrix);
}
