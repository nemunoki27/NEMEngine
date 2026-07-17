#include "SetUIProgressDelayedCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <limits>

//============================================================================
//	SetUIProgressDelayedCommand internal
//============================================================================
namespace {

	bool IsDirectChild(Engine::ECSWorld& world, const Engine::Entity& parent,
		const Engine::Entity& child) {

		if (!world.IsAlive(parent) || !world.IsAlive(child)) {
			return false;
		}
		const auto* hierarchy = world.TryGetComponent<Engine::HierarchyComponent>(child);
		const auto* parentSceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(parent);
		if (!hierarchy || !parentSceneObject) {
			return false;
		}
		return hierarchy->parent == parent ||
			hierarchy->parentLocalFileID == parentSceneObject->localFileID;
	}

	Engine::Entity ResolveDelayedEntity(Engine::ECSWorld& world,
		const Engine::Entity& target, Engine::UUID localFileID) {

		if (!localFileID) {
			return Engine::Entity::Null();
		}
		const Engine::Entity delayedEntity =
			Engine::SceneObjectUtility::FindByLocalFileID(world, localFileID);
		return IsDirectChild(world, target, delayedEntity) ?
			delayedEntity : Engine::Entity::Null();
	}
}

//============================================================================
//	SetUIProgressDelayedCommand classMethods
//============================================================================
Engine::SetUIProgressDelayedCommand::SetUIProgressDelayedCommand(
	const Entity& targetEntity, bool delayed) :
	initialTarget_(targetEntity), delayed_(delayed) {
}

bool Engine::SetUIProgressDelayedCommand::ApplyComponent(
	EditorCommandContext& context, const nlohmann::json& data) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	const Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target) || !world->HasComponent<UIProgressComponent>(target)) {
		return false;
	}

	world->AddComponentFromJson(target, "UIProgress", data);
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetUIProgressDelayedCommand::EnableDelayed(
	EditorCommandContext& context, const Entity& target) {

	ECSWorld* world = context.GetWorld();
	if (!world || !world->HasComponent<UIProgressComponent>(target) ||
		!world->HasComponent<SpriteRendererComponent>(target)) {
		return false;
	}

	auto& progress = world->GetComponent<UIProgressComponent>(target);
	Entity delayedEntity = ResolveDelayedEntity(*world, target, progress.delayedTargetLocalFileID);
	if (!world->IsAlive(delayedEntity)) {

		const SpriteRendererComponent sourceSprite =
			world->GetComponent<SpriteRendererComponent>(target);
		const auto* sourceUVTransform = world->TryGetComponent<UVTransformComponent>(target);
		const UVTransformComponent sourceUV = sourceUVTransform ?
			*sourceUVTransform : UVTransformComponent{};

		delayedEntity = world->CreateEntity();
		SceneAuthoring::EnsureGameObjectDefaults(*world, delayedEntity, "Delayed Fill");

		const auto& targetSceneObject = world->GetComponent<SceneObjectComponent>(target);
		auto& delayedSceneObject = world->GetComponent<SceneObjectComponent>(delayedEntity);
		delayedSceneObject.sceneInstanceID = targetSceneObject.sceneInstanceID;
		delayedSceneObject.sourceAsset = targetSceneObject.sourceAsset;

		HierarchySystem hierarchySystem;
		hierarchySystem.SetParent(*world, delayedEntity, target);

		auto& delayedSprite = world->AddComponent<SpriteRendererComponent>(delayedEntity);
		delayedSprite = sourceSprite;
		if ((std::numeric_limits<int32_t>::min)() < delayedSprite.order) {
			--delayedSprite.order;
		}
		world->AddComponent<UVTransformComponent>(delayedEntity) = sourceUV;
		changedDelayedEntity_ = true;
	}

	progress.delayed = true;
	progress.delayedTargetLocalFileID =
		world->GetComponent<SceneObjectComponent>(delayedEntity).localFileID;
	ResetUIProgressRuntime(progress);

	if (changedDelayedEntity_) {
		EditorEntitySnapshotUtility::CaptureSubtree(
			*world, delayedEntity, delayedEntitySnapshot_);
	}
	context.RebuildHierarchyAll();
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetUIProgressDelayedCommand::DisableDelayed(
	EditorCommandContext& context, const Entity& target) {

	ECSWorld* world = context.GetWorld();
	if (!world || !world->HasComponent<UIProgressComponent>(target)) {
		return false;
	}

	auto& progress = world->GetComponent<UIProgressComponent>(target);
	const Entity delayedEntity =
		ResolveDelayedEntity(*world, target, progress.delayedTargetLocalFileID);
	if (world->IsAlive(delayedEntity)) {

		EditorEntitySnapshotUtility::CaptureSubtree(
			*world, delayedEntity, delayedEntitySnapshot_);
		changedDelayedEntity_ = !delayedEntitySnapshot_.IsEmpty();
	}

	progress.delayed = false;
	progress.delayedTargetLocalFileID = {};
	ResetUIProgressRuntime(progress);
	if (changedDelayedEntity_) {
		EditorEntitySnapshotUtility::DestroySubtree(*world, delayedEntity);
	}
	context.RebuildHierarchyAll();
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetUIProgressDelayedCommand::RestoreDelayedEntity(
	EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || delayedEntitySnapshot_.IsEmpty()) {
		return false;
	}
	if (world->IsAlive(world->FindByUUID(delayedEntitySnapshot_.rootStableUUID))) {
		return true;
	}

	const std::vector<Entity> restoredEntities =
		EditorEntitySnapshotUtility::RestoreSubtree(*world, delayedEntitySnapshot_);
	EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(
		context, *world, delayedEntitySnapshot_, restoredEntities);
	context.RebuildHierarchyAll();
	return world->IsAlive(world->FindByUUID(delayedEntitySnapshot_.rootStableUUID));
}

void Engine::SetUIProgressDelayedCommand::DestroyDelayedEntity(
	EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || delayedEntitySnapshot_.IsEmpty()) {
		return;
	}

	const Entity delayedEntity =
		world->FindByUUID(delayedEntitySnapshot_.rootStableUUID);
	if (world->IsAlive(delayedEntity)) {
		EditorEntitySnapshotUtility::DestroySubtree(*world, delayedEntity);
		context.RebuildHierarchyAll();
	}
}

bool Engine::SetUIProgressDelayedCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !world->IsAlive(initialTarget_) ||
		!world->HasComponent<UIProgressComponent>(initialTarget_)) {
		return false;
	}

	targetStableUUID_ = world->GetUUID(initialTarget_);
	const auto& progress = world->GetComponent<UIProgressComponent>(initialTarget_);
	if (progress.delayed == delayed_) {
		return false;
	}
	if (!world->SerializeComponentToJson(initialTarget_, "UIProgress", beforeData_)) {
		return false;
	}

	const bool result = delayed_ ?
		EnableDelayed(context, initialTarget_) :
		DisableDelayed(context, initialTarget_);
	if (!result || !world->SerializeComponentToJson(initialTarget_, "UIProgress", afterData_)) {
		return false;
	}
	return true;
}

void Engine::SetUIProgressDelayedCommand::Undo(EditorCommandContext& context) {

	if (delayed_) {
		ApplyComponent(context, beforeData_);
		if (changedDelayedEntity_) {
			DestroyDelayedEntity(context);
		}
		return;
	}

	if (changedDelayedEntity_) {
		RestoreDelayedEntity(context);
	}
	ApplyComponent(context, beforeData_);
}

bool Engine::SetUIProgressDelayedCommand::Redo(EditorCommandContext& context) {

	if (delayed_) {
		if (changedDelayedEntity_ && !RestoreDelayedEntity(context)) {
			return false;
		}
		return ApplyComponent(context, afterData_);
	}

	if (!ApplyComponent(context, afterData_)) {
		return false;
	}
	if (changedDelayedEntity_) {
		DestroyDelayedEntity(context);
	}
	return true;
}
