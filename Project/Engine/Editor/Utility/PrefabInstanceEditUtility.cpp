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

	// 編集中プレファブの最上位ルートか
	bool IsPrefabEditRoot(const Engine::EditorContext* editorContext,
		Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!editorContext || !editorContext->isPrefabEditing || !editorContext->prefabEditInstanceID ||
			!world.IsAlive(entity) || !world.HasComponent<Engine::PrefabLinkComponent>(entity)) {
			return false;
		}
		const auto& link = world.GetComponent<Engine::PrefabLinkComponent>(entity);
		return link.isPrefabRoot && link.prefabInstanceID == editorContext->prefabEditInstanceID;
	}

	// Prefabの構造を直接変更できるEntityか
	bool CanEditPrefabStructure(const Engine::EditorContext* editorContext,
		Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::PrefabLinkComponent>(entity)) {
			return world.IsAlive(entity);
		}

		const auto& link = world.GetComponent<Engine::PrefabLinkComponent>(entity);
		if (!editorContext || !editorContext->isPrefabEditing) {
			return link.isPrefabRoot;
		}
		if (link.prefabInstanceID == editorContext->prefabEditInstanceID) {
			return !link.isPrefabRoot;
		}
		return link.isPrefabRoot;
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

bool Engine::PrefabInstanceEditUtility::CanUnpack(
	const EditorContext* editorContext, ECSWorld& world, const Entity& entity) {

	return IsPrefabRoot(world, entity) && !IsPrefabEditRoot(editorContext, world, entity);
}

bool Engine::PrefabInstanceEditUtility::CanDelete(
	const EditorContext* editorContext, ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity)) {
		return false;
	}
	return CanEditPrefabStructure(editorContext, world, entity) &&
		!IsPrefabEditRoot(editorContext, world, entity);
}

bool Engine::PrefabInstanceEditUtility::CanChangeParent(const EditorContext* editorContext,
	ECSWorld& world, const Entity& entity, const Entity& newParent) {

	if (!world.IsAlive(entity)) {
		return false;
	}
	if (world.IsAlive(newParent) && entity == newParent) {
		return false;
	}
	if (editorContext && editorContext->isPrefabEditing && world.IsAlive(newParent) &&
		world.HasComponent<PrefabLinkComponent>(newParent) &&
		world.GetComponent<PrefabLinkComponent>(newParent).prefabInstanceID !=
		editorContext->prefabEditInstanceID) {

		return false;
	}

	return CanEditPrefabStructure(editorContext, world, entity) &&
		!IsPrefabEditRoot(editorContext, world, entity);
}

bool Engine::PrefabInstanceEditUtility::CanChangeSiblingOrder(const EditorContext* editorContext,
	ECSWorld& world, const Entity& entity, const Entity& anchor) {

	if (!world.IsAlive(entity) || !world.IsAlive(anchor)) {
		return false;
	}
	return CanEditPrefabStructure(editorContext, world, entity) &&
		CanEditPrefabStructure(editorContext, world, anchor) &&
		!IsPrefabEditRoot(editorContext, world, entity) &&
		!IsPrefabEditRoot(editorContext, world, anchor);
}
