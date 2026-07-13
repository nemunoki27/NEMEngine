#include "PrefabInstanceEditUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

//============================================================================
//	PrefabInstanceEditUtility classMethods
//============================================================================
namespace {

	// Entityの親を返す
	Engine::Entity GetParent(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return Engine::Entity::Null();
		}
		const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
		return world.IsAlive(hierarchy.parent) ? hierarchy.parent : Engine::Entity::Null();
	}

	// サブツリーにPrefab由来Entityがあるか
	bool ContainsPrefabEntity(Engine::ECSWorld& world, const Engine::Entity& root) {

		if (!world.IsAlive(root)) {
			return false;
		}
		if (world.HasComponent<Engine::PrefabLinkComponent>(root)) {
			return true;
		}
		if (!world.HasComponent<Engine::HierarchyComponent>(root)) {
			return false;
		}
		Engine::Entity child = world.GetComponent<Engine::HierarchyComponent>(root).firstChild;
		while (world.IsAlive(child)) {

			if (ContainsPrefabEntity(world, child)) {
				return true;
			}
			child = world.HasComponent<Engine::HierarchyComponent>(child) ?
				world.GetComponent<Engine::HierarchyComponent>(child).nextSibling : Engine::Entity::Null();
		}
		return false;
	}
}

bool Engine::PrefabInstanceEditUtility::IsPrefabEntity(ECSWorld& world, const Entity& entity) {

	return world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity);
}

bool Engine::PrefabInstanceEditUtility::IsPrefabRoot(ECSWorld& world, const Entity& entity) {

	return IsPrefabEntity(world, entity) && world.GetComponent<PrefabLinkComponent>(entity).isPrefabRoot;
}

bool Engine::PrefabInstanceEditUtility::IsInPrefabInstance(ECSWorld& world, const Entity& entity) {

	Entity cursor = entity;
	while (world.IsAlive(cursor)) {

		if (IsPrefabEntity(world, cursor)) {
			return true;
		}
		cursor = GetParent(world, cursor);
	}
	return false;
}

bool Engine::PrefabInstanceEditUtility::CanDelete(
	const EditorContext* editorContext, ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity) || !IsPrefabEntity(world, entity)) {
		return world.IsAlive(entity);
	}
	if (editorContext && editorContext->isPrefabEditing) {

		// Prefab編集では編集対象のルートだけ削除できない
		return !IsPrefabRoot(world, entity) || world.IsAlive(GetParent(world, entity));
	}

	// Sceneではインスタンス全体のルートだけ削除できる
	return IsPrefabRoot(world, entity);
}

bool Engine::PrefabInstanceEditUtility::CanChangeParent(const EditorContext* editorContext,
	ECSWorld& world, const Entity& entity, const Entity& newParent) {

	if (!world.IsAlive(entity)) {
		return false;
	}
	if (editorContext && editorContext->isPrefabEditing) {

		// Prefab編集の対象ルートは階層ルートのまま保つ
		return !IsPrefabRoot(world, entity) || world.IsAlive(GetParent(world, entity));
	}
	if (!IsPrefabEntity(world, entity)) {

		// Nested Prefabを含むサブツリーは他Prefabの階層へ入れない
		return !world.IsAlive(newParent) || !IsInPrefabInstance(world, newParent) ||
			!ContainsPrefabEntity(world, entity);
	}
	if (!IsPrefabRoot(world, entity)) {
		return false;
	}

	// Scene上のPrefabルート配置は変更できるが別Prefabの子にはできない
	return !world.IsAlive(newParent) || !IsInPrefabInstance(world, newParent);
}

bool Engine::PrefabInstanceEditUtility::CanChangeSiblingOrder(const EditorContext* editorContext,
	ECSWorld& world, const Entity& entity, const Entity& anchor) {

	if (!world.IsAlive(entity) || !world.IsAlive(anchor)) {
		return false;
	}
	if (editorContext && editorContext->isPrefabEditing) {

		// Prefab編集の対象ルートは並び替えない
		return !IsPrefabRoot(world, entity) || world.IsAlive(GetParent(world, entity));
	}

	const Entity parent = GetParent(world, entity);
	if (world.IsAlive(parent) && IsPrefabEntity(world, parent)) {

		// Prefab配下では追加Entity同士の並び替えだけ許可する
		return !IsPrefabEntity(world, entity) && !IsPrefabEntity(world, anchor);
	}

	// Scene直下ではPrefabルートを通常のScene Entityと同様に並び替えられる
	return (!IsPrefabEntity(world, entity) || IsPrefabRoot(world, entity)) &&
		(!IsPrefabEntity(world, anchor) || IsPrefabRoot(world, anchor));
}
