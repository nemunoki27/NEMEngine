#include "UnpackPrefabCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Utility/PrefabInstanceEditUtility.h>

// c++
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	UnpackPrefabCommand classMethods
//============================================================================
namespace {

	using PrefabLinkMap = std::unordered_map<Engine::UUID, Engine::PrefabLinkComponent>;

	// World内のPrefabリンクを安定UUIDで取得する
	PrefabLinkMap CapturePrefabLinks(Engine::ECSWorld& world) {

		PrefabLinkMap links;
		world.ForEach<Engine::PrefabLinkComponent>(
			[&](const Engine::Entity& entity, Engine::PrefabLinkComponent& link) {

				links.emplace(world.GetUUID(entity), link);
			});
		return links;
	}

	// Prefabリンクの保存値が一致するか
	bool IsSameLink(const Engine::PrefabLinkComponent& lhs, const Engine::PrefabLinkComponent& rhs) {

		return lhs.prefabAsset == rhs.prefabAsset &&
			lhs.prefabLocalFileID == rhs.prefabLocalFileID &&
			lhs.prefabInstanceID == rhs.prefabInstanceID &&
			lhs.ownerPrefabInstanceID == rhs.ownerPrefabInstanceID &&
			lhs.nestedSlotID == rhs.nestedSlotID &&
			lhs.isPrefabAssetNested == rhs.isPrefabAssetNested &&
			lhs.isPrefabRoot == rhs.isPrefabRoot;
	}
}

Engine::UnpackPrefabCommand::UnpackPrefabCommand(const Entity& root, PrefabUnpackMode mode) :
	initialRoot_(root), mode_(mode) {
}

bool Engine::UnpackPrefabCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene()) {
		return false;
	}
	ECSWorld* world = context.GetWorld();
	if (!world || !world->IsAlive(initialRoot_) ||
		!PrefabInstanceEditUtility::CanUnpack(context.editorContext, *world, initialRoot_)) {
		return false;
	}

	rootStableUUID_ = world->GetUUID(initialRoot_);
	const PrefabLinkMap before = CapturePrefabLinks(*world);
	PrefabSystem prefabSystem{};
	if (!prefabSystem.UnpackPrefabInstance(*world, initialRoot_, mode_)) {
		return false;
	}
	const PrefabLinkMap after = CapturePrefabLinks(*world);

	std::unordered_set<UUID> entityUUIDs;
	for (const auto& entry : before) {

		entityUUIDs.insert(entry.first);
	}
	for (const auto& entry : after) {

		entityUUIDs.insert(entry.first);
	}
	changes_.clear();
	for (UUID entityUUID : entityUUIDs) {

		const auto beforeIt = before.find(entityUUID);
		const auto afterIt = after.find(entityUUID);
		if (beforeIt != before.end() && afterIt != after.end() &&
			IsSameLink(beforeIt->second, afterIt->second)) {
			continue;
		}

		LinkChange change{};
		change.entityStableUUID = entityUUID;
		if (beforeIt != before.end()) {
			change.before = beforeIt->second;
		}
		if (afterIt != after.end()) {
			change.after = afterIt->second;
		}
		changes_.emplace_back(std::move(change));
	}

	if (changes_.empty()) {
		return false;
	}
	if (context.editorState) {
		context.editorState->SelectEntity(initialRoot_);
	}
	return true;
}

void Engine::UnpackPrefabCommand::Undo(EditorCommandContext& context) {

	ApplyState(context, false);
}

bool Engine::UnpackPrefabCommand::Redo(EditorCommandContext& context) {

	return ApplyState(context, true);
}

bool Engine::UnpackPrefabCommand::ApplyState(EditorCommandContext& context, bool useAfter) const {

	if (!context.CanEditScene()) {
		return false;
	}
	ECSWorld* world = context.GetWorld();
	if (!world || changes_.empty()) {
		return false;
	}
	for (const LinkChange& change : changes_) {

		if (!world->IsAlive(world->FindByUUID(change.entityStableUUID))) {
			return false;
		}
	}

	for (const LinkChange& change : changes_) {

		const Entity entity = world->FindByUUID(change.entityStableUUID);
		const std::optional<PrefabLinkComponent>& link = useAfter ? change.after : change.before;
		if (link) {

			if (world->HasComponent<PrefabLinkComponent>(entity)) {
				world->GetComponent<PrefabLinkComponent>(entity) = *link;
				world->MarkComponentModified<PrefabLinkComponent>(entity);
			} else {
				world->AddComponent<PrefabLinkComponent>(entity) = *link;
			}
		} else if (world->HasComponent<PrefabLinkComponent>(entity)) {

			world->RemoveComponent<PrefabLinkComponent>(entity);
		}
	}

	if (context.editorState) {
		context.editorState->SelectEntity(world->FindByUUID(rootStableUUID_));
	}
	return true;
}
