#include "JointAttachmentUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

//============================================================================
//	JointAttachmentUtility internalMethods
//============================================================================
namespace {

	// ワールド行列を分解してTransformのローカルSRTへ設定する、ジョイント追従の相対値になる
	void ApplyLocalFromMatrix(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::Matrix4x4& localMatrix) {

		if (!world.HasComponent<Engine::TransformComponent>(entity)) {
			return;
		}
		Engine::Vector3 position{};
		Engine::Quaternion rotation{};
		Engine::Vector3 scale{};
		if (!Engine::DecomposeAffine3D(localMatrix, position, rotation, scale)) {
			return;
		}
		auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
		transform.localPos = position;
		transform.localRotation = rotation;
		transform.localScale = scale;
		transform.isDirty = true;
	}
}

//============================================================================
//	JointAttachmentUtility classMethods
//============================================================================
void Engine::JointAttachmentUtility::Attach(ECSWorld& world, HierarchySystem& hierarchySystem,
	const Entity& entity, const Entity& skinnedEntity, const std::string& jointName) {

	// 自分自身や無効な対象には付けない
	if (!world.IsAlive(entity) || !world.IsAlive(skinnedEntity) || entity == skinnedEntity) {
		return;
	}
	if (!world.HasComponent<TransformComponent>(entity)) {
		return;
	}
	// ジョイントが存在するか確認しておく、無効なジョイントには付けない
	Matrix4x4 jointWorld{};
	if (!GetJointWorldMatrix(world, skinnedEntity, jointName, jointWorld)) {
		return;
	}

	// エンティティ親から切り離してルートにし、ジョイント駆動へ切り替える
	hierarchySystem.SetParent(world, entity, Entity::Null());

	// ジョイント参照を設定する
	UUID skinnedLocalFileID{};
	if (world.HasComponent<SceneObjectComponent>(skinnedEntity)) {
		skinnedLocalFileID = world.GetComponent<SceneObjectComponent>(skinnedEntity).localFileID;
	}
	if (!world.HasComponent<JointAttachmentComponent>(entity)) {
		world.AddComponent<JointAttachmentComponent>(entity);
	}
	auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
	attachment.skinnedEntityLocalFileID = skinnedLocalFileID;
	attachment.jointName = jointName;

	// ローカルSRTをリセットしてジョイント原点へ合わせる、付け替えても確実にジョイントへ移動する
	auto& transform = world.GetComponent<TransformComponent>(entity);
	transform.localPos = Vector3::AnyInit(0.0f);
	transform.localRotation = Quaternion::Identity();
	transform.localScale = Vector3::AnyInit(1.0f);
	transform.isDirty = true;
}

void Engine::JointAttachmentUtility::Detach(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity) || !world.HasComponent<JointAttachmentComponent>(entity)) {
		return;
	}
	// 解除前のワールド行列を控える、ルートに戻してもワールド位置を維持する
	Matrix4x4 currentWorld = Matrix4x4::Identity();
	if (world.HasComponent<TransformComponent>(entity)) {
		currentWorld = world.GetComponent<TransformComponent>(entity).worldMatrix;
	}
	world.RemoveComponent<JointAttachmentComponent>(entity);

	// ルートなのでローカル = ワールドとして設定する
	ApplyLocalFromMatrix(world, entity, currentWorld);
}
