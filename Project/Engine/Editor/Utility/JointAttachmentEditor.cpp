#include "JointAttachmentEditor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

//============================================================================
//	JointAttachmentEditor internalMethods
//============================================================================
namespace {

	// 行列を分解してローカル変換へ反映する
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
		Engine::MarkTransformSubtreeDirty(world, entity);
	}
}

//============================================================================
//	JointAttachmentEditor classMethods
//============================================================================
void Engine::JointAttachmentEditor::Attach(ECSWorld& world, HierarchySystem& hierarchySystem, const Entity& entity,
	const Entity& skinnedEntity, const std::string& jointName) {

	// 自分自身や無効な対象には付けない
	if (!world.IsAlive(entity) || !world.IsAlive(skinnedEntity) || entity == skinnedEntity) {
		return;
	}
	if (!world.HasComponent<TransformComponent>(entity)) {
		return;
	}
	if (!HierarchyUtility::CanSetParent(world, entity, skinnedEntity)) {
		return;
	}
	if (!world.HasComponent<SceneObjectComponent>(skinnedEntity)) {
		return;
	}
	const UUID skinnedLocalFileID = world.GetComponent<SceneObjectComponent>(skinnedEntity).localFileID;
	if (!skinnedLocalFileID) {
		return;
	}
	// 接続先のJointを検証する
	Matrix4x4 jointWorld{};
	if (!JointAttachmentUtility::GetJointWorldMatrix(world, skinnedEntity, jointName, jointWorld)) {
		return;
	}

	// Entityの親から切り離してJoint駆動へ切り替える
	hierarchySystem.SetParent(world, entity, Entity::Null());

	// ジョイント参照を設定する
	if (!world.HasComponent<JointAttachmentComponent>(entity)) {
		world.AddComponent<JointAttachmentComponent>(entity);
	}
	auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
	attachment.skinnedEntityLocalFileID = skinnedLocalFileID;
	attachment.jointName = jointName;

	// ローカル変換を初期化してJoint原点へ合わせる
	auto& transform = world.GetComponent<TransformComponent>(entity);
	transform.localPos = Vector3::AnyInit(0.0f);
	transform.localRotation = Quaternion::Identity();
	transform.localScale = Vector3::AnyInit(1.0f);
	MarkTransformSubtreeDirty(world, entity);
}

void Engine::JointAttachmentEditor::Detach(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity) || !world.HasComponent<JointAttachmentComponent>(entity)) {
		return;
	}
	// 解除前のワールド行列を保持する
	Matrix4x4 currentWorld = Matrix4x4::Identity();
	if (world.HasComponent<TransformComponent>(entity)) {
		currentWorld = world.GetComponent<TransformComponent>(entity).worldMatrix;
	}
	world.RemoveComponent<JointAttachmentComponent>(entity);

	// ルートのローカル変換へワールド行列を反映する
	ApplyLocalFromMatrix(world, entity, currentWorld);
}
