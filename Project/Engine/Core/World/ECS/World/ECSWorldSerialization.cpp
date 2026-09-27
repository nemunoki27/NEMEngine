#include "ECSWorldSerialization.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingComponent.h>
#include <Engine/Core/World/ECS/World/WorldCommandExecutor.h>

// c++
#include <algorithm>

using namespace Engine;

//============================================================================
//	ECSWorldSerialization classMethods
//============================================================================
bool ECSWorldSerialization::AddComponentFromJson(ECSWorld& world,
	const Entity& entity, const std::string_view& typeName, const nlohmann::json& data) {

	// エンティティが有効でなければ追加できない
	world.AssertAlive(entity);

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}
	world.ValidateComponentStorage(*info);

	// 既に持っているなら上書きする
	const bool added = !world.records_[entity.index].location.archetype->Has(info->id);
	if (added) {

		// シグネチャを更新してアーキタイプを移動する
		EntitySignature oldSignature = world.records_[entity.index].location.archetype->GetSignature();
		EntitySignature newSignature = oldSignature;
		newSignature.Set(info->id);
		// 新しいアーキタイプへ移動する
		world.MigrateEntity(entity, oldSignature, newSignature);
		world.CompleteComponentChange(entity, info->id, ComponentMutationKind::Added);
	}

	return world.ApplyComponentJson(entity, typeName, data);
}

bool ECSWorldSerialization::ApplyComponentJson(ECSWorld& world,
	const Entity& entity, const std::string_view& typeName, const nlohmann::json& data) {

	if (!world.IsAlive(entity)) {
		return false;
	}

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}

	auto& location = world.records_[entity.index].location;
	if (!location.archetype->Has(info->id)) {
		return false;
	}

	void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, info->id);
	info->fromJson(world, entity, ptr, data);
	world.NotifyComponentMutation(entity, info->id, ComponentMutationKind::Modified);
	return true;
}

std::unique_ptr<ECSWorld> ECSWorldSerialization::CloneForSerialization(const ECSWorld& world) {

	ECSWorld::QueryScope query(world);
	auto snapshot = std::make_unique<ECSWorld>(world.kind_);
	snapshot->records_.resize(world.records_.size());
	snapshot->freeHead_ = world.freeHead_;
	snapshot->nextComponentInstanceID_ = world.nextComponentInstanceID_;
	snapshot->uuidToEntity_.reserve(world.uuidToEntity_.size());
	snapshot->changes_.CopySerializationRevisionsFrom(world.changes_);

	ComponentTypeRegistry& registry =
		ComponentTypeRegistry::GetInstance();
	std::vector<const PendingComponent*> pending;
	for (uint32_t index = 0;
		index < static_cast<uint32_t>(
			world.records_.size()); ++index) {
		snapshot->records_[index].generation =
			world.records_[index].generation;
		snapshot->records_[index].nextFree = world.records_[index].nextFree;
	}

	// 同じArchetypeの列解決を行ごとに繰り返さず、Chunkを連続走査する
	for (const auto& [sourceSignature,
		sourceArchetypeOwner] : world.archetypes_) {

		const EntityArchetype& sourceArchetype =
			*sourceArchetypeOwner;
		EntitySignature signature{};
		std::vector<const ComponentTypeInfo*> infos{};
		std::vector<uint32_t> sourceColumns{};
		std::vector<uint32_t> destinationColumns{};
		for (uint32_t typeID :
			sourceArchetype.GetTypes()) {

			const ComponentTypeInfo& info =
				registry.GetInfo(typeID);
			// custom serializerが参照する従属Bufferも保存用Worldへ複製する
			if (!info.serializable &&
				info.storageKind !=
				ComponentStorageKind::Buffer) {
				continue;
			}
			signature.Set(typeID);
			infos.emplace_back(&info);
			sourceColumns.emplace_back(
				sourceArchetype.GetColumnIndex(typeID));
		}

		EntityArchetype* destinationArchetype =
			snapshot->GetOrCreateArchetype(signature);
		destinationColumns.reserve(infos.size());
		for (const ComponentTypeInfo* info : infos) {
			destinationColumns.emplace_back(
				destinationArchetype->
					GetColumnIndex(info->id));
		}

		for (const std::unique_ptr<EntityChunk>&
			sourceChunkOwner :
			sourceArchetype.GetChunks()) {

			const EntityChunk& sourceChunk =
				*sourceChunkOwner;
			const std::span<const Entity> entities =
				sourceChunk.GetEntities();
			for (uint32_t sourceRow = 0;
				sourceRow <
					static_cast<uint32_t>(
						entities.size()); ++sourceRow) {

				const Entity entity =
					entities[sourceRow];
				const EntityRecord& sourceRecord =
					world.records_[entity.index];
				EntityRecord& destinationRecord =
					snapshot->records_[entity.index];
				// 追加待ちの型を保存用の格納先へ含める
				world.commandBuffer_.CollectPendingComponents(entity, pending);
				std::erase_if(pending, [&](const PendingComponent* component) {
					const auto& info = component->GetInfo();
					return sourceRecord.pendingDestroy || sourceArchetype.Has(info.id) ||
						(!info.serializable && info.storageKind != ComponentStorageKind::Buffer);
				});
				EntityArchetype* rowArchetype = destinationArchetype;
				if (!pending.empty()) {
					EntitySignature rowSignature = signature;
					for (const auto* component : pending) rowSignature.Set(component->GetInfo().id);
					rowArchetype = snapshot->GetOrCreateArchetype(rowSignature);
				}
				const auto [destinationChunkIndex,
					destinationRow] =
					rowArchetype->
						AddUninitialized(entity);
				EntityChunk& destinationChunk =
					*rowArchetype->GetChunks()[
						destinationChunkIndex];

				destinationRecord.alive = true;
				destinationRecord.pendingDestroy =
					sourceRecord.pendingDestroy;
				destinationRecord.uuid =
					sourceRecord.uuid;
				destinationRecord.location = {
					rowArchetype,
					destinationChunkIndex,
					destinationRow
				};
				snapshot->uuidToEntity_[
					destinationRecord.uuid] = entity;

				for (size_t column = 0;
					column < infos.size(); ++column) {

					const ComponentTypeInfo& info =
						*infos[column];
					const uint32_t destinationColumn = rowArchetype == destinationArchetype ?
						destinationColumns[column] : rowArchetype->GetColumnIndex(info.id);
					const void* source =
						sourceChunk.
							GetRawByColumnIndex(
								sourceColumns[column],
								sourceRow);
					destinationChunk.CopyConstructByColumnIndex(destinationColumn, destinationRow, source,
						sourceChunk.GetComponentInstanceID(sourceColumns[column], sourceRow));
					if (info.enableable) {
						destinationChunk.
							SetEnabledByColumnIndex(
								destinationColumn,
								destinationRow,
								sourceChunk.
									IsEnabledByColumnIndex(
										sourceColumns[column],
										sourceRow));
					}
				}
				// 値だけを複製し、追加通知や外部サービスを動かさない
				for (const PendingComponent* component : pending) {
					destinationChunk.CopyConstructByColumnIndex(rowArchetype->GetColumnIndex(component->GetInfo().id),
						destinationRow, component->GetData(), component->GetInstanceID());
				}
			}
		}
	}

	std::vector<WorldCommand> commands;
	world.commandBuffer_.CollectUnappliedCommands(commands);
	for (const WorldCommand& command : commands) {

		// Sceneロードは保存用Worldへ反映しない
		if (command.kind == WorldCommandKind::LoadSceneAdditive ||
			command.kind == WorldCommandKind::LoadSceneSingle ||
			command.kind == WorldCommandKind::UnloadScene) {
			continue;
		}
		// 追加値は先に保存用Archetypeへ複製済み
		if (command.kind == WorldCommandKind::AddComponentValue) {
			continue;
		}
		WorldCommandExecutor::Apply(*snapshot, command);
	}
	return snapshot;
}

void ECSWorldSerialization::SerializeEntityComponents(const ECSWorld& world,
	const Entity& entity, nlohmann::json& outComponents) {

	Assert::Call(world.IsAlive(entity), "Entityが有効ではありません");

	// アーキタイプから持っているコンポーネントの種類を取得
	const EntityArchetype* archetype = world.records_[entity.index].location.archetype;
	const auto& types = archetype->GetTypes();

	outComponents = nlohmann::json::object();
	for (auto typeID : types) {

		const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
		if (!info.serializable) {
			continue;
		}

		// アーキタイプからコンポーネントデータを取得
		auto& location = world.records_[entity.index].location;
		const void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);

		// jsonに変換して出力
		info.toJson(world, entity, ptr, outComponents[info.name]);
	}
	// 追加直後の設定値も構造変更を進めず保存する
	std::vector<const PendingComponent*> pending;
	world.commandBuffer_.CollectPendingComponents(entity, pending);
	for (const PendingComponent* component : pending) {
		const auto& info = component->GetInfo();
		if (info.serializable && !archetype->Has(info.id)) {
			info.toJson(world, entity, component->GetData(), outComponents[info.name]);
		}
	}
}

bool ECSWorldSerialization::SerializeComponentToJson(const ECSWorld& world,
	const Entity& entity, const std::string_view& typeName, nlohmann::json& outData) {

	// エンティティが有効でなければシリアライズできない
	if (!world.IsAlive(entity)) {
		return false;
	}

	// コンポーネントの種類IDを取得
	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}
	if (!info->serializable) {
		return false;
	}

	const auto& location = world.records_[entity.index].location;
	if (!location.archetype->Has(info->id)) {
		const PendingComponent* pending = world.commandBuffer_.FindPendingComponent(entity, info->id);
		if (!pending || pending->GetInstanceID() == 0) return false;
		info->toJson(world, entity, pending->GetData(), outData);
		return true;
	}
	// アーキタイプからコンポーネントデータを取得
	const void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, info->id);
	info->toJson(world, entity, ptr, outData);
	return true;
}
