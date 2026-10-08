#include "HierarchyCommandContractTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/ReparentEntitiesCommand.h>
#include <Engine/Editor/Commands/Entity/ReorderEntityCommand.h>
#include <Engine/Editor/Commands/Entity/CreateEntityCommand.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentSystem.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>

// c++
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

	using namespace Engine;

	// 通知中の親の構造変更でも子の非アクティブ状態を維持する
	bool TestActivePropagationMutation() {

		ECSWorld world;
		const Entity root = SceneAuthoring::CreateGameObject(world, "Root");
		const Entity first = SceneAuthoring::CreateGameObject(world, "First");
		const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
		const Entity spare = SceneAuthoring::CreateGameObject(world, "Spare");
		HierarchySystem hierarchy;
		hierarchy.SetParent(world, first, root);
		hierarchy.SetParent(world, second, root);

		// 先行する子の通知で親を別のArchetypeへ移す
		struct MutationState {

			Entity root;		  // 構造を変える親
			Entity trigger;		  // 通知を受ける子
			bool applied = false; // 構造変更の実行済み
		};
		MutationState state{root, first};
		const uint64_t listener = world.AddComponentMutationListener(
			[](ECSWorld& world, const Entity& entity, [[maybe_unused]] uint32_t typeID, ComponentMutationKind kind,
				void* userData) {
				auto& state = *static_cast<MutationState*>(userData);
				if (!state.applied && entity == state.trigger && kind == ComponentMutationKind::Modified) {
					state.applied = true;
					world.AddComponent<PointLightComponent>(state.root);
				}
			},
			&state);
		world.GetComponent<SceneObjectComponent>(root).activeSelf = false;
		hierarchy.UpdateActiveInHierarchy(world, root);
		world.RemoveComponentMutationListener(listener);
		return state.applied && world.HasComponent<PointLightComponent>(root) &&
			   !world.GetComponent<SceneObjectComponent>(root).activeInHierarchy &&
			   !world.GetComponent<SceneObjectComponent>(first).activeInHierarchy &&
			   !world.GetComponent<SceneObjectComponent>(second).activeInHierarchy &&
			   world.GetComponent<SceneObjectComponent>(spare).activeInHierarchy;
	}

	// 同じ文書内IDを持つ別Sceneの親とJointを混同しない
	bool TestSceneScopedHierarchyLinks() {

		ECSWorld world;
		std::vector<Entity> scope;
		for (uint64_t scene = 1; scene <= 2; ++scene) {

			for (uint64_t local = 1; local <= 3; ++local) {

				const Entity entity = SceneAuthoring::CreateGameObject(world, "Scoped");
				auto& membership = world.GetComponent<SceneObjectComponent>(entity);
				membership.sceneInstanceID = Engine::UUID{scene};
				membership.localFileID = Engine::UUID{local};
				if (local == 2) {
					world.GetComponent<HierarchyComponent>(entity).parentLocalFileID = Engine::UUID{1};
				} else if (local == 3) {
					world.AddComponent<JointAttachmentComponent>(entity).skinnedEntityLocalFileID = Engine::UUID{1};
				}
				scope.emplace_back(entity);
			}
		}
		HierarchySystem hierarchy;
		hierarchy.RebuildRuntimeLinks(world, scope);
		if (HierarchyUtility::GetParent(world, scope[1]) != scope[0] ||
			HierarchyUtility::GetParent(world, scope[4]) != scope[3]) {
			return false;
		}
		const auto subtree = HierarchyUtility::CollectLogicalSubtree(world, scope[0]);
		const std::vector<Entity> ownSelection{scope[0], scope[2]};
		const std::vector<Entity> otherSelection{scope[0], scope[5]};
		return subtree.size() == 3 && std::ranges::find(subtree, scope[1]) != subtree.end() &&
			   std::ranges::find(subtree, scope[2]) != subtree.end() &&
			   HierarchyUtility::CollectLogicalRoots(world, ownSelection) == std::vector<Entity>{scope[0]} &&
			   HierarchyUtility::CollectLogicalRoots(world, otherSelection) == otherSelection;
	}

	// Joint配下のTransform欠損を除いて後続の子を更新する
	bool TestJointTransformBranches() {

		ECSWorld world;
		const Entity skinned = SceneAuthoring::CreateGameObject(world, "Skinned");
		const Entity attached = SceneAuthoring::CreateGameObject(world, "Attached");
		const Entity missing = SceneAuthoring::CreateGameObject(world, "Missing");
		const Entity child = SceneAuthoring::CreateGameObject(world, "Child");
		world.AddComponent<SkinnedAnimationComponent>(skinned);
		SkinnedAnimationRuntimeData* runtime = TryGetSkinnedAnimationRuntime(world, skinned);
		if (!runtime) {
			return false;
		}
		runtime->skeleton.joints.emplace_back();
		runtime->skeleton.joints.front().name = "Hand";
		runtime->skeleton.joints.front().skeletonSpaceMatrix = Matrix4x4::MakeTranslateMatrix({3, 0, 0});
		runtime->skeleton.jointMap["Hand"] = 0;
		auto& attachment = world.AddComponent<JointAttachmentComponent>(attached);
		attachment.skinnedEntityLocalFileID = world.GetComponent<SceneObjectComponent>(skinned).localFileID;
		attachment.jointName = "Hand";
		world.GetComponent<TransformComponent>(skinned).worldMatrix = Matrix4x4::MakeTranslateMatrix({4, 0, 0});
		world.GetComponent<TransformComponent>(attached).localPos = {1, 0, 0};
		world.GetComponent<TransformComponent>(child).localPos = {2, 0, 0};
		HierarchySystem hierarchy;
		hierarchy.SetParent(world, missing, attached);
		hierarchy.SetParent(world, child, attached);
		world.RemoveComponent<TransformComponent>(missing);
		world.AddComponent<SpriteRendererComponent>(attached);
		world.AddComponent<SpriteRendererComponent>(child);
		world.AddComponent<PointLightComponent>(child);
		// Joint名とScene参照から同じ行列を取得する
		Entity resolved{};
		Matrix4x4 matrix{};
		if (!JointAttachmentUtility::ResolveAttachedJoint(world, attached, resolved, matrix) || resolved != skinned ||
			matrix.m[3][0] != 3.0f || !JointAttachmentUtility::GetAttachedJointWorldMatrix(world, attached, matrix) ||
			matrix.m[3][0] != 7.0f || !JointAttachmentUtility::GetJointWorldMatrix(world, skinned, "Hand", matrix) ||
			matrix.m[3][0] != 7.0f || JointAttachmentUtility::GetJointWorldMatrix(world, skinned, "Absent", matrix) ||
			matrix.m[3][0] != 7.0f) {
			return false;
		}
		JointAttachmentSystem joints;
		SystemContext context{};
		const uint64_t before = world.GetRenderTransformRevision();
		const uint64_t lightingBefore = world.GetLightDataRevision();
		joints.LateUpdate(world, context);
		std::vector<Entity> changed;
		if (world.GetComponent<TransformComponent>(attached).worldMatrix.m[3][0] != 8.0f ||
			world.GetComponent<TransformComponent>(child).worldMatrix.m[3][0] != 10.0f ||
			world.GetComponent<TransformComponent>(child).isDirty || world.GetRenderTransformRevision() != before + 1 ||
			world.GetLightDataRevision() != lightingBefore + 1 || !world.CollectRenderTransformChanges(before, changed) ||
			changed.size() != 2 || std::ranges::find(changed, attached) == changed.end() ||
			std::ranges::find(changed, child) == changed.end()) {
			return false;
		}
		// 同じポーズでは描画と照明の変更世代を進めない
		joints.LateUpdate(world, context);
		if (world.GetRenderTransformRevision() != before + 1 || world.GetLightDataRevision() != lightingBefore + 1) {
			return false;
		}
		runtime->skeleton.joints.front().skeletonSpaceMatrix = Matrix4x4::MakeTranslateMatrix({6, 0, 0});
		joints.LateUpdate(world, context);
		return world.GetComponent<TransformComponent>(attached).worldMatrix.m[3][0] == 11.0f &&
			   world.GetComponent<TransformComponent>(child).worldMatrix.m[3][0] == 13.0f &&
			   world.GetRenderTransformRevision() == before + 2 && world.GetLightDataRevision() == lightingBefore + 2;
	}

	// 対象自身と子Entityをルートの兄弟順へ含めない
	bool TestRootSiblingOrder() {

		ECSWorld world;
		const Entity target = SceneAuthoring::CreateGameObject(world, "Target");
		if (HierarchyUtility::FindMaxRootSiblingOrder(world, target) != -1) {
			return false;
		}
		const Entity parent = SceneAuthoring::CreateGameObject(world, "Parent");
		const Entity child = SceneAuthoring::CreateGameObject(world, "Child");
		HierarchySystem hierarchy;
		hierarchy.SetParent(world, child, parent);
		world.GetComponent<HierarchyComponent>(target).siblingOrder = 100;
		world.GetComponent<HierarchyComponent>(parent).siblingOrder = 4;
		world.GetComponent<HierarchyComponent>(child).siblingOrder = 50;
		return HierarchyUtility::FindMaxRootSiblingOrder(world, target) == 4 &&
			   HierarchyUtility::FindMaxRootSiblingOrder(world, Entity::Null()) == 100;
	}

	// 兄弟順の上限では生成を取り消し既存の並びを維持する
	bool TestRootSiblingOrderLimit() {

		ECSWorld world;
		const Entity root = SceneAuthoring::CreateGameObject(world, "Root");
		int32_t order = -1;
		if (!HierarchyUtility::TryGetNextRootSiblingOrder(world, root, order) || order != 0) {
			return false;
		}
		world.GetComponent<HierarchyComponent>(root).siblingOrder = std::numeric_limits<int32_t>::max() - 1;
		if (!HierarchyUtility::TryGetNextRootSiblingOrder(world, Entity::Null(), order) ||
			order != std::numeric_limits<int32_t>::max()) {
			return false;
		}
		world.GetComponent<HierarchyComponent>(root).siblingOrder = order;
		order = 12;
		if (HierarchyUtility::TryGetNextRootSiblingOrder(world, Entity::Null(), order) || order != 12) {
			return false;
		}
		EditorContext editor;
		editor.activeWorld = &world;
		EditorState state;
		state.SelectEntity(root);
		EditorCommandContext context;
		context.editorContext = &editor;
		context.editorState = &state;
		CreateEntityCommand command("Rejected", {}, EntityCreationPreset::Empty, Dimension::Type3D);
		const auto countAlive = [&] {
			size_t count = 0;
			world.ForEachAliveEntity([&](Entity) { ++count; });
			return count;
		};
		if (command.Execute(context) || countAlive() != 1 || state.GetSelectedEntities() != std::vector<Entity>{root} ||
			world.GetComponent<HierarchyComponent>(root).siblingOrder != std::numeric_limits<int32_t>::max()) {
			return false;
		}
		// 上限の解消後は同じ操作を再実行できる
		world.GetComponent<HierarchyComponent>(root).siblingOrder = 3;
		if (!command.Execute(context) || countAlive() != 2) {
			return false;
		}
		command.Undo(context);
		return countAlive() == 1 && world.GetComponent<HierarchyComponent>(root).siblingOrder == 3;
	}

	// 再実行とUndoで初回の親を維持する
	bool TestReparentRollbackAndRetry() {

		ECSWorld world;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorState state;
		EditorCommandContext context;
		context.editorContext = &editor;
		context.editorState = &state;
		const Entity oldParent = SceneAuthoring::CreateGameObject(world, "Old");
		const Entity first = SceneAuthoring::CreateGameObject(world, "First");
		const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
		const Entity destination = SceneAuthoring::CreateGameObject(world, "Destination");
		HierarchySystem hierarchy;
		hierarchy.SetParent(world, first, oldParent);
		hierarchy.SetParent(world, destination, second);
		ReparentEntitiesCommand command({first, second}, world.GetUUID(destination));
		// 2件目の循環で1件目を元の親へ戻す
		if (command.Execute(context) || HierarchyUtility::GetParent(world, first) != oldParent ||
			HierarchyUtility::GetParent(world, destination) != second || HierarchyUtility::GetParent(world, second).IsValid()) {
			return false;
		}
		hierarchy.SetParent(world, destination, Entity::Null());
		if (!command.Execute(context) || HierarchyUtility::GetParent(world, first) != destination ||
			HierarchyUtility::GetParent(world, second) != destination) {
			return false;
		}
		command.Undo(context);
		if (HierarchyUtility::GetParent(world, first) != oldParent || HierarchyUtility::GetParent(world, second).IsValid()) {
			return false;
		}
		return command.Redo(context) && HierarchyUtility::GetParent(world, first) == destination &&
			   HierarchyUtility::GetParent(world, second) == destination;
	}

	// 別Sceneへの親変更で適用済みの変更も取り消す
	bool TestReparentSceneBoundary() {

		ECSWorld world;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorCommandContext context;
		context.editorContext = &editor;
		const Entity destination = SceneAuthoring::CreateGameObject(world, "Destination");
		const Entity first = SceneAuthoring::CreateGameObject(world, "First");
		const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
		world.GetComponent<SceneObjectComponent>(destination).sceneInstanceID = Engine::UUID{1};
		world.GetComponent<SceneObjectComponent>(first).sceneInstanceID = Engine::UUID{1};
		world.GetComponent<SceneObjectComponent>(second).sceneInstanceID = Engine::UUID{2};
		ReparentEntitiesCommand command({first, second}, world.GetUUID(destination));
		return !command.Execute(context) && !HierarchyUtility::GetParent(world, first).IsValid() &&
			   !HierarchyUtility::GetParent(world, second).IsValid() &&
			   world.GetComponent<SceneObjectComponent>(second).sceneInstanceID == Engine::UUID{2};
	}

	// Root並替えで別Sceneの順序を変更しない
	bool TestRootReorderSceneBoundary() {

		ECSWorld world;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorCommandContext context;
		context.editorContext = &editor;
		const Entity first = SceneAuthoring::CreateGameObject(world, "First");
		const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
		const Entity other = SceneAuthoring::CreateGameObject(world, "Other");
		world.GetComponent<SceneObjectComponent>(first).sceneInstanceID = Engine::UUID{1};
		world.GetComponent<SceneObjectComponent>(second).sceneInstanceID = Engine::UUID{1};
		world.GetComponent<SceneObjectComponent>(other).sceneInstanceID = Engine::UUID{2};
		world.GetComponent<HierarchyComponent>(first).siblingOrder = 0;
		world.GetComponent<HierarchyComponent>(second).siblingOrder = 1;
		world.GetComponent<HierarchyComponent>(other).siblingOrder = 50;
		ReorderEntityCommand command(first, second, true);
		if (!command.Execute(context) || world.GetComponent<HierarchyComponent>(first).siblingOrder != 1 ||
			world.GetComponent<HierarchyComponent>(second).siblingOrder != 0 ||
			world.GetComponent<HierarchyComponent>(other).siblingOrder != 50) {
			return false;
		}
		command.Undo(context);
		if (world.GetComponent<HierarchyComponent>(first).siblingOrder != 0 ||
			world.GetComponent<HierarchyComponent>(second).siblingOrder != 1 ||
			world.GetComponent<HierarchyComponent>(other).siblingOrder != 50) {
			return false;
		}
		return command.Redo(context) && world.GetComponent<HierarchyComponent>(first).siblingOrder == 1 &&
			   world.GetComponent<HierarchyComponent>(other).siblingOrder == 50;
	}
}

bool NEMTests::TestHierarchyCommandContracts() {

	return TestActivePropagationMutation() && TestSceneScopedHierarchyLinks() && TestJointTransformBranches() &&
		   TestRootSiblingOrder() && TestRootSiblingOrderLimit() && TestReparentRollbackAndRetry() &&
		   TestReparentSceneBoundary() && TestRootReorderSceneBoundary();
}
