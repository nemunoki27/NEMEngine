#include "TransformSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

//============================================================================
//	TransformSystem classMethods
//============================================================================
void Engine::TransformSystem::FixedUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	UpdateTransforms(world);
}

void Engine::TransformSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	UpdateTransforms(world);
}

void Engine::TransformSystem::UpdateDirtySubtree(ECSWorld& world, const Entity& entity) {

	auto& transform = world.GetComponent<TransformComponent>(entity);
	const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);

	// 階層先頭は現在の親ワールド行列から更新する
	if (hierarchy.parent.IsValid() && world.IsAlive(hierarchy.parent) &&
		world.HasComponent<TransformComponent>(hierarchy.parent)) {

		const Matrix4x4& parentWorld =
			world.GetComponent<TransformComponent>(hierarchy.parent).worldMatrix;
		const Matrix4x4 followParent = BuildParentFollowMatrix(parentWorld,
			transform.ignoreParentScale, transform.ignoreParentRotation);
		transform.worldMatrix = MakeLocalMatrix(transform) * followParent;
	} else {

		transform.worldMatrix = MakeLocalMatrix(transform);
	}
	transform.isDirty = false;

	stack_.clear();
	stack_.push_back({ entity, true });
	while (!stack_.empty()) {

		// スタックから親エンティティを取り出す
		const StackNode node = stack_.back();
		stack_.pop_back();

		if (!world.IsAlive(node.entity) ||
			!world.HasComponent<HierarchyComponent>(node.entity) ||
			!world.HasComponent<TransformComponent>(node.entity)) {
			continue;
		}

		const auto& parentHierarchy = world.GetComponent<HierarchyComponent>(node.entity);
		const Matrix4x4& parentWorld =
			world.GetComponent<TransformComponent>(node.entity).worldMatrix;

		// 親変更の影響を受ける子孫だけを走査する
		Entity child = parentHierarchy.firstChild;
		while (child.IsValid()) {

			if (!world.IsAlive(child) ||
				!world.HasComponent<HierarchyComponent>(child)) {
				break;
			}
			auto& childHierarchy = world.GetComponent<HierarchyComponent>(child);
			const Entity nextSibling = childHierarchy.nextSibling;
			auto* childTransform = world.TryGetComponent<TransformComponent>(child);
			if (!childTransform) {
				child = nextSibling;
				continue;
			}

			// 非アクティブな部分木は再有効化までdirtyを保持する
			const bool updateWorld =
				node.updateWorld && IsEntityActiveInHierarchy(world, child);
			if (updateWorld) {

				const Matrix4x4 followParent = BuildParentFollowMatrix(parentWorld,
					childTransform->ignoreParentScale, childTransform->ignoreParentRotation);
				childTransform->worldMatrix = MakeLocalMatrix(*childTransform) * followParent;
				childTransform->isDirty = false;
			} else {

				childTransform->isDirty = true;
			}
			stack_.push_back({ child, updateWorld });
			child = nextSibling;
		}
	}
}

void Engine::TransformSystem::UpdateTransforms(ECSWorld& world) {

	dirtyTransforms_.clear();
	// 連続配置されたTransform列からdirtyなエンティティだけを集める
	world.ForEach<TransformComponent, HierarchyComponent>([&](
		Entity entity, TransformComponent& transform, HierarchyComponent&) {
			if (transform.isDirty && IsEntityActiveInHierarchy(world, entity)) {
				dirtyTransforms_.emplace_back(entity);
			}
		});
	for (const Entity entity : dirtyTransforms_) {

		if (!world.IsAlive(entity) ||
			!world.HasComponent<TransformComponent>(entity) ||
			!world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}
		auto& transform = world.GetComponent<TransformComponent>(entity);
		if (!transform.isDirty) {
			continue;
		}

		const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		// dirtyな親が同じ走査内で部分木を更新するため、子からの重複更新を避ける
		if (hierarchy.parent.IsValid() && world.IsAlive(hierarchy.parent) &&
			world.HasComponent<TransformComponent>(hierarchy.parent) &&
			world.HasComponent<HierarchyComponent>(hierarchy.parent) &&
			world.GetComponent<TransformComponent>(hierarchy.parent).isDirty &&
			IsEntityActiveInHierarchy(world, hierarchy.parent)) {
			continue;
		}
		UpdateDirtySubtree(world, entity);
	}
}
