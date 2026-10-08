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

// c++
#include <cstdint>

namespace {

	// 名前と範囲を確認してJoint行列を取得する
	bool TryGetSkeletonSpaceMatrix(
		const Engine::ECSWorld& world, Engine::Entity entity, const std::string& jointName, Engine::Matrix4x4& matrix) {

		const auto* runtime = Engine::TryGetSkinnedAnimationRuntime(world, entity);
		if (!runtime) {
			return false;
		}
		const int32_t jointIndex = Engine::FindSkeletonJointIndex(runtime->skeleton, jointName);
		if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(runtime->skeleton.joints.size())) {
			return false;
		}
		matrix = runtime->skeleton.joints[jointIndex].skeletonSpaceMatrix;
		return true;
	}
}

//============================================================================
//	JointAttachmentUtility classMethods
//============================================================================
bool Engine::JointAttachmentUtility::ResolveAttachedJoint(
	const ECSWorld& world, const Entity& entity, Entity& outSkinnedEntity, Matrix4x4& outSkeletonSpaceMatrix) {

	if (!world.IsAlive(entity) || !world.HasComponent<JointAttachmentComponent>(entity)) {
		return false;
	}
	const auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
	if (attachment.jointName.empty() || !attachment.skinnedEntityLocalFileID) {
		return false;
	}

	// 同じSceneの文書内IDから接続先を解決する
	const UUID sceneInstanceID = SceneObjectUtility::GetSceneInstanceID(world, entity);
	outSkinnedEntity = SceneObjectUtility::FindByLocalFileID(world, sceneInstanceID, attachment.skinnedEntityLocalFileID);
	if (!world.IsAlive(outSkinnedEntity) || !world.HasComponent<SkinnedAnimationComponent>(outSkinnedEntity)) {
		return false;
	}

	return TryGetSkeletonSpaceMatrix(world, outSkinnedEntity, attachment.jointName, outSkeletonSpaceMatrix);
}

bool Engine::JointAttachmentUtility::GetJointWorldMatrix(
	const ECSWorld& world, const Entity& skinnedEntity, const std::string& jointName, Matrix4x4& outWorldMatrix) {

	if (!world.IsAlive(skinnedEntity) || !world.HasComponent<SkinnedAnimationComponent>(skinnedEntity) ||
		!world.HasComponent<TransformComponent>(skinnedEntity)) {
		return false;
	}
	// 骨格空間から親のワールド空間へ変換する
	Matrix4x4 jointSkeletonSpace{};
	if (!TryGetSkeletonSpaceMatrix(world, skinnedEntity, jointName, jointSkeletonSpace)) {
		return false;
	}
	const Matrix4x4& skinnedWorld = world.GetComponent<TransformComponent>(skinnedEntity).worldMatrix;
	outWorldMatrix = jointSkeletonSpace * skinnedWorld;
	return true;
}

bool Engine::JointAttachmentUtility::GetAttachedJointWorldMatrix(
	const ECSWorld& world, const Entity& entity, Matrix4x4& outWorldMatrix) {

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
