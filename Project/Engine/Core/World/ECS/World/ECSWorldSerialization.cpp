#include "ECSWorldSerialization.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

using namespace Engine;

//============================================================================
//	ECSWorldSerialization classMethods
//============================================================================
void ECSWorldSerialization::AddComponentFromJson(ECSWorld& world,
	const Entity& entity, const std::string_view& typeName, const nlohmann::json& data) {

	// エンティティが有効でなければ追加できない
	world.AssertAlive(entity);

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		Assert::Call(false, "シーンファイルに未登録のComponentType名があります");
		return;
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

	world.ApplyComponentJson(entity, typeName, data);
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
				const auto [destinationChunkIndex,
					destinationRow] =
					destinationArchetype->
						AddUninitialized(entity);
				EntityChunk& destinationChunk =
					*destinationArchetype->GetChunks()[
						destinationChunkIndex];

				destinationRecord.alive = true;
				destinationRecord.pendingDestroy =
					sourceRecord.pendingDestroy;
				destinationRecord.uuid =
					sourceRecord.uuid;
				destinationRecord.location = {
					destinationArchetype,
					destinationChunkIndex,
					destinationRow
				};
				snapshot->uuidToEntity_[
					destinationRecord.uuid] = entity;

				for (size_t column = 0;
					column < infos.size(); ++column) {

					const ComponentTypeInfo& info =
						*infos[column];
					const void* source =
						sourceChunk.
							GetRawByColumnIndex(
								sourceColumns[column],
								sourceRow);
					destinationChunk.CopyConstructByColumnIndex(destinationColumns[column], destinationRow, source,
						sourceChunk.GetComponentInstanceID(sourceColumns[column], sourceRow));
					if (info.enableable) {
						destinationChunk.
							SetEnabledByColumnIndex(
								destinationColumns[column],
								destinationRow,
								sourceChunk.
									IsEnabledByColumnIndex(
										sourceColumns[column],
										sourceRow));
					}
				}
			}
		}
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
		void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);

		// jsonに変換して出力
		info.toJson(world, entity, ptr, outComponents[info.name]);
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
		return false;
	}
	// アーキタイプからコンポーネントデータを取得
	void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, info->id);
	info->toJson(world, entity, ptr, outData);
	return true;
}
