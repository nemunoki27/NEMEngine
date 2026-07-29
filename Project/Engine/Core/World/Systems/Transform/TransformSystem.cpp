#include "TransformSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

// c++
#include <algorithm>

//============================================================================
//	TransformSystem classMethods
//============================================================================
void Engine::TransformSystem::OnWorldEnter(
	ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	transformTypeID_ =
		ComponentTypeRegistry::GetInstance().GetID<TransformComponent>();
	mutationListenerID_ = world.AddComponentMutationListener(
		&TransformSystem::OnComponentMutation, this);
	QueueExistingDirtyTransforms(world);
}

void Engine::TransformSystem::OnWorldExit(
	ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	if (mutationListenerID_ != 0) {
		world.RemoveComponentMutationListener(mutationListenerID_);
	}
	mutationListenerID_ = 0;
	transformTypeID_ = 0;
	dirtyTransforms_.clear();
	queuedTransforms_.clear();
	changedTransforms_.clear();
	stack_.clear();
}

void Engine::TransformSystem::FixedUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	UpdateTransforms(world);
}

void Engine::TransformSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	UpdateTransforms(world);
}

void Engine::TransformSystem::OnSceneInstancesChanged(
	ECSWorld& world, [[maybe_unused]] SystemContext& context,
	[[maybe_unused]] SceneChangePhase phase) {

	QueueExistingDirtyTransforms(world);
}

void Engine::TransformSystem::OnComponentMutation(
	[[maybe_unused]] ECSWorld& world, const Entity& entity,
	uint32_t typeID, ComponentMutationKind kind, void* userData) {

	auto* system = static_cast<TransformSystem*>(userData);
	if (!system || typeID != system->transformTypeID_ ||
		kind == ComponentMutationKind::Removed) {
		return;
	}
	system->queuedTransforms_.emplace_back(entity);
}

void Engine::TransformSystem::QueueExistingDirtyTransforms(
	ECSWorld& world) {

	world.ForEach<TransformComponent>([&](
		Entity entity, TransformComponent& transform) {
		if (transform.isDirty) {
			queuedTransforms_.emplace_back(entity);
		}
		});
}

Engine::ComponentChangeChannel Engine::TransformSystem::UpdateDirtySubtree(
	ECSWorld& world, const Entity& entity) {

	auto& transform = world.GetComponent<TransformComponent>(entity);
	const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
	ComponentChangeChannel changedChannels =
		world.GetTransformChangeChannels(entity);
	if (changedChannels != ComponentChangeChannel::None) {
		changedTransforms_.emplace_back(entity);
	}

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
				const ComponentChangeChannel childChannels =
					world.GetTransformChangeChannels(child);
				changedChannels |= childChannels;
				if (childChannels !=
					ComponentChangeChannel::None) {
					changedTransforms_.emplace_back(child);
				}
			} else {

				childTransform->isDirty = true;
			}
			stack_.push_back({ child, updateWorld });
			child = nextSibling;
		}
	}
	return changedChannels;
}

void Engine::TransformSystem::UpdateTransforms(ECSWorld& world) {

	if (queuedTransforms_.empty()) {
		return;
	}

	dirtyTransforms_.clear();
	dirtyTransforms_.swap(queuedTransforms_);
	std::sort(dirtyTransforms_.begin(), dirtyTransforms_.end(),
		[](const Entity& lhs, const Entity& rhs) {
			if (lhs.index != rhs.index) {
				return lhs.index < rhs.index;
			}
			return lhs.generation < rhs.generation;
		});
	dirtyTransforms_.erase(
		std::unique(dirtyTransforms_.begin(), dirtyTransforms_.end()),
		dirtyTransforms_.end());

	bool updated = false;
	ComponentChangeChannel changedChannels =
		ComponentChangeChannel::None;
	changedTransforms_.clear();
	for (const Entity entity : dirtyTransforms_) {

		if (!world.IsAlive(entity) ||
			!world.HasComponent<TransformComponent>(entity) ||
			!world.HasComponent<HierarchyComponent>(entity) ||
			!IsEntityActiveInHierarchy(world, entity)) {
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
		changedChannels |= UpdateDirtySubtree(world, entity);
		updated = true;
	}
	if (updated) {
		// 部分木内の全Transformを個別通知せず、影響する抽出世代だけを一度進める
		world.MarkDataModified();
		world.MarkTransformConsumersModified(
			changedChannels, changedTransforms_);
	}
}
