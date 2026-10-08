#include "ECSWorldSerialization.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingComponent.h>
#include <Engine/Core/World/ECS/World/WorldCommandExecutor.h>

// c++
#include <algorithm>
#include <stdexcept>

using namespace Engine;

//============================================================================
//	ECSWorldSerialization classMethods
//============================================================================
bool ECSWorldSerialization::AddComponentFromJson(
	ECSWorld& world, const Entity& entity, std::string_view typeName, const nlohmann::json& data) {

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

	// 追加通知で変更された入力名を読み直さない
	return world.ApplyComponentJson(entity, info->name, data);
}

bool ECSWorldSerialization::ApplyComponentJson(
	ECSWorld& world, const Entity& entity, std::string_view typeName, const nlohmann::json& data) {

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
	const auto lifetime = world.GetLifetime();
	const uint64_t instanceID = world.GetComponentInstanceID(entity, info->id);
	const auto storage = world.storageState_;
	info->fromJson(world, entity, ptr, data);
	// hookで終了したWorldや置き換わった個体へ通知しない
	lifetime->ThrowIfEnded();
	if (world.GetComponentInstanceID(entity, info->id) != instanceID) {
		return false;
	}
	world.NotifyComponentMutation(entity, info->id, ComponentMutationKind::Modified);
	return true;
}

std::unique_ptr<ECSWorld> ECSWorldSerialization::CloneForSerialization(const ECSWorld& world) {

	const auto lifetime = world.GetLifetime();
	ECSWorld::QueryScope query(world);
	auto snapshot = std::make_unique<ECSWorld>(world.kind_);
	snapshot->records_.resize(world.records_.size());
	snapshot->freeHead_ = world.freeHead_;
	snapshot->nextComponentInstanceID_ = world.nextComponentInstanceID_;
	snapshot->uuidToEntity_.reserve(world.uuidToEntity_.size());
	snapshot->changes_.CopySerializationRevisionsFrom(world.changes_);

	ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
	std::vector<std::shared_ptr<const PendingComponent>> pending;
	for (uint32_t index = 0; index < static_cast<uint32_t>(world.records_.size()); ++index) {
		snapshot->records_[index].generation = world.records_[index].generation;
		snapshot->records_[index].nextFree = world.records_[index].nextFree;
	}

	// 列を一度解決してChunkを連続走査する
	for (const auto& [sourceSignature, sourceArchetypeOwner] : world.storageState_->archetypes) {

		const EntityArchetype& sourceArchetype = *sourceArchetypeOwner;
		EntitySignature signature{};
		std::vector<const ComponentTypeInfo*> infos{};
		std::vector<uint32_t> sourceColumns{};
		std::vector<uint32_t> destinationColumns{};
		for (uint32_t typeID : sourceArchetype.GetTypes()) {

			const ComponentTypeInfo& info = registry.GetInfo(typeID);
			// 保存hookが参照する従属Bufferも複製する
			if (!info.serializable && info.storageKind != ComponentStorageKind::Buffer) {
				continue;
			}
			signature.Set(typeID);
			infos.emplace_back(&info);
			sourceColumns.emplace_back(sourceArchetype.GetColumnIndex(typeID));
		}

		EntityArchetype* destinationArchetype = &snapshot->GetOrCreateArchetype(signature);
		destinationColumns.reserve(infos.size());
		for (const ComponentTypeInfo* info : infos) {
			destinationColumns.emplace_back(destinationArchetype->GetColumnIndex(info->id));
		}

		for (uint32_t sourceChunkIndex = 0; sourceChunkIndex < sourceArchetype.GetChunkCount(); ++sourceChunkIndex) {

			const EntityChunk& sourceChunk = sourceArchetype.GetChunk(sourceChunkIndex);
			const std::span<const Entity> entities = sourceChunk.GetEntities();
			for (uint32_t sourceRow = 0; sourceRow < static_cast<uint32_t>(entities.size()); ++sourceRow) {

				const Entity entity = entities[sourceRow];
				const EntityRecord& sourceRecord = world.records_[entity.index];
				EntityRecord& destinationRecord = snapshot->records_[entity.index];
				// 追加待ちの型を保存用の格納先へ含める
				world.commandBuffer_.CollectPendingComponents(entity, pending);
				std::erase_if(pending, [&](const auto& component) {
					const auto& info = component->GetInfo();
					return sourceRecord.pendingDestroy || sourceArchetype.Has(info.id) ||
						   (!info.serializable && info.storageKind != ComponentStorageKind::Buffer);
				});
				EntityArchetype* rowArchetype = destinationArchetype;
				if (!pending.empty()) {
					EntitySignature rowSignature = signature;
					for (const auto& component : pending) {
						rowSignature.Set(component->GetInfo().id);
					}
					rowArchetype = &snapshot->GetOrCreateArchetype(rowSignature);
				}
				const auto [destinationChunkIndex, destinationRow] = rowArchetype->AddUninitialized(entity);
				EntityChunk& destinationChunk = rowArchetype->GetChunk(destinationChunkIndex);

				destinationRecord.alive = true;
				destinationRecord.pendingDestroy = sourceRecord.pendingDestroy;
				destinationRecord.uuid = sourceRecord.uuid;
				destinationRecord.location = {rowArchetype, destinationChunkIndex, destinationRow};
				snapshot->uuidToEntity_[destinationRecord.uuid] = entity;

				for (size_t column = 0; column < infos.size(); ++column) {

					const ComponentTypeInfo& info = *infos[column];
					const uint32_t destinationColumn = rowArchetype == destinationArchetype
														   ? destinationColumns[column]
														   : rowArchetype->GetColumnIndex(info.id);
					const void* source = sourceChunk.GetRawByColumnIndex(sourceColumns[column], sourceRow);
					destinationChunk.CopyConstructByColumnIndex(destinationColumn, destinationRow, source,
						sourceChunk.GetComponentInstanceID(sourceColumns[column], sourceRow));
					// コピー処理で終了した元Worldへ戻らない
					lifetime->ThrowIfEnded();
					if (info.enableable) {
						destinationChunk.SetEnabledByColumnIndex(destinationColumn, destinationRow,
							sourceChunk.IsEnabledByColumnIndex(sourceColumns[column], sourceRow));
					}
				}
				// 値だけを複製し、追加通知や外部サービスを動かさない
				const auto checkPending = [&](const auto& component) {
					if (!component->GetInstanceID() ||
						world.commandBuffer_.FindPendingComponent(entity, component->GetInfo().id) != component.get()) {
						throw std::runtime_error("保存用複製中にComponentの追加予約が変更されました");
					}
				};
				for (const auto& component : pending) {
					checkPending(component);
					const auto value = component->AcquireValueLease();
					destinationChunk.CopyConstructByColumnIndex(rowArchetype->GetColumnIndex(component->GetInfo().id),
						destinationRow, component->GetData(), component->GetInstanceID());
					lifetime->ThrowIfEnded();
				}
				// 後続のコピーで取り消された予約も公開しない
				for (const auto& component : pending) {
					checkPending(component);
				}
			}
		}
	}

	std::vector<WorldCommand> commands;
	world.commandBuffer_.CollectUnappliedCommands(commands);
	for (const WorldCommand& command : commands) {

		// Sceneロードは保存用Worldへ反映しない
		if (command.kind == WorldCommandKind::LoadSceneAdditive || command.kind == WorldCommandKind::LoadSceneSingle ||
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

void ECSWorldSerialization::SerializeEntityComponents(
	const ECSWorld& world, const Entity& entity, nlohmann::json& outComponents) {

	Assert::Call(world.IsAlive(entity), "Entityが有効ではありません");
	const auto lifetime = world.GetLifetime();
	// 保存hookの途中で格納先を移動しない
	ECSWorld::QueryScope query(world);

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
		lifetime->ThrowIfEnded();
	}
	// 予約取消に備えて保存中の値を保持する
	std::vector<std::shared_ptr<const PendingComponent>> pending;
	world.commandBuffer_.CollectPendingComponents(entity, pending);
	for (const auto& component : pending) {
		const auto& info = component->GetInfo();
		if (component->GetInstanceID() && world.commandBuffer_.FindPendingComponent(entity, info.id) == component.get() &&
			info.serializable && !archetype->Has(info.id)) {
			const auto value = component->AcquireValueLease();
			info.toJson(world, entity, component->GetData(), outComponents[info.name]);
			lifetime->ThrowIfEnded();
			if (!component->GetInstanceID() || world.commandBuffer_.FindPendingComponent(entity, info.id) != component.get()) {
				outComponents.erase(info.name);
			}
		}
	}
}

bool ECSWorldSerialization::SerializeComponentToJson(
	const ECSWorld& world, const Entity& entity, std::string_view typeName, nlohmann::json& outData) {

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
	const auto lifetime = world.GetLifetime();
	ECSWorld::QueryScope query(world);

	const auto& location = world.records_[entity.index].location;
	if (!location.archetype->Has(info->id)) {
		std::vector<std::shared_ptr<const PendingComponent>> pending;
		world.commandBuffer_.CollectPendingComponents(entity, pending);
		const auto found = std::ranges::find_if(pending, [&](const auto& value) { return value->GetInfo().id == info->id; });
		if (found == pending.end()) {
			return false;
		}
		const auto value = (*found)->AcquireValueLease();
		info->toJson(world, entity, (*found)->GetData(), outData);
		lifetime->ThrowIfEnded();
		return (*found)->GetInstanceID() != 0 && world.commandBuffer_.FindPendingComponent(entity, info->id) == found->get();
	}
	// アーキタイプからコンポーネントデータを取得
	const void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, info->id);
	info->toJson(world, entity, ptr, outData);
	lifetime->ThrowIfEnded();
	return true;
}
