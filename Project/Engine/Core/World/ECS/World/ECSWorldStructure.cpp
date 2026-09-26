#include "ECSWorld.h"
#include "PendingComponent.h"

//============================================================================
//	include
//============================================================================
// c++
#include <exception>
#include <stdexcept>

using namespace Engine;

//============================================================================
//	ECSWorld classMethods
//============================================================================
Entity ECSWorld::CreateEntityInArchetype(EntityArchetype* archetype, UUID stableUUID) {

	const std::vector<uint32_t> initialTypes = archetype->GetTypes();
	Entity entity{};
	UUID uuid{};
	Entity previousEntity = Entity::Null();
	{
		StructuralScope scope(*this);
		const uint64_t firstInstanceID = ReserveComponentInstanceIDs(initialTypes.size());
		uuid = stableUUID ? stableUUID : UUID::New();
		const auto previous = uuidToEntity_.find(uuid);
		previousEntity = previous != uuidToEntity_.end() ? previous->second : Entity::Null();
		const uint32_t index = AllocateIndex();
		entity = { index, records_[index].generation };
		bool rowCreated = false;
		try {

			// Componentの構築後にEntityを公開する
			auto [chunkIndex, row] = archetype->Add(entity, firstInstanceID);
			rowCreated = true;
			records_[index].location = { archetype, chunkIndex, row };
			records_[index].uuid = uuid;
			records_[index].alive = true;
			uuidToEntity_[uuid] = entity;
			for (uint32_t typeID : initialTypes) {
				const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
				info.initializeStorage(*this, entity, archetype->GetRaw(chunkIndex, row, typeID));
			}
		} catch (...) {

			// 構築済みの外部データと行を巻き戻す
			std::exception_ptr failure = std::current_exception();
			if (rowCreated) {
				try {
					ReleaseExternalComponents(entity, nullptr);
				} catch (...) {
					failure = std::current_exception();
				}
				const EntityLocation location = records_[index].location;
				const Entity moved = archetype->RemoveSwap(location.chunkIndex, location.row);
				if (moved.IsValid()) {
					records_[moved.index].location = location;
				}
			}
			const auto current = uuidToEntity_.find(uuid);
			if (current != uuidToEntity_.end() && current->second == entity) {
				if (previousEntity.IsValid()) {
					current->second = previousEntity;
				} else {
					uuidToEntity_.erase(current);
				}
			}
			RecycleIndex(index);
			std::rethrow_exception(failure);
		}
	}

	try {
		for (uint32_t typeID : initialTypes) {

			// 関連Componentの追加後も現在位置から引き直す
			if (!HasComponent(entity, typeID)) {
				continue;
			}
			const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
			const EntityLocation location = records_[entity.index].location;
			info.onAdded(*this, entity, location.archetype->GetRaw(location.chunkIndex, location.row, typeID));
		}
		if (!IsAlive(entity)) {
			throw std::runtime_error("初期化中にEntityが削除されました");
		}
	} catch (...) {
		// 追加通知の失敗でも元のUUID対応を復元する
		if (IsAlive(previousEntity)) {
			const auto current = uuidToEntity_.find(uuid);
			if (current == uuidToEntity_.end()) {
				uuidToEntity_.emplace(uuid, previousEntity);
			} else if (current->second == entity) {
				current->second = previousEntity;
			}
		}
		DestroyEntityImmediate(entity);
		throw;
	}
	MarkDataModified();
	const ComponentChangeChannel channels = GetChangeChannels(entity);
	if (HasComponentChangeChannel(channels, ComponentChangeChannel::Render)) {
		MarkRenderDataModified(entity);
	}
	if (HasComponentChangeChannel(channels, ComponentChangeChannel::Lighting)) {
		changes_.MarkLightDataModified();
	}
	return entity;
}

void ECSWorld::DestroyEntityImmediate(const Entity& entity) {

	if (!IsAlive(entity) || records_[entity.index].destroying) {
		return;
	}
	if (queryDepth_ != 0 || structuralChange_) {
		DestroyEntity(entity);
		return;
	}

	// 通知から同じEntityを二重破棄させない
	records_[entity.index].destroying = true;
	std::exception_ptr failure;
	try {
		NotifyComponentMutation(entity, UINT32_MAX, ComponentMutationKind::EntityDestroyed);
	} catch (...) {
		failure = std::current_exception();
	}
	{
		StructuralScope scope(*this);
		try {
			ReleaseExternalComponents(entity, nullptr);
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}

		// 通知中の他Entityの移動を反映した現在位置から抜く
		const EntityLocation location = records_[entity.index].location;
		const Entity moved = location.archetype->RemoveSwap(location.chunkIndex, location.row);
		if (moved.IsValid()) {
			records_[moved.index].location = location;
		}
		const auto entry = uuidToEntity_.find(records_[entity.index].uuid);
		if (entry != uuidToEntity_.end() && entry->second == entity) {
			uuidToEntity_.erase(entry);
		}
		RecycleIndex(entity.index);
	}
	if (failure) {
		std::rethrow_exception(failure);
	}
}

uint32_t ECSWorld::AllocateIndex() {

	if (freeHead_ != UINT32_MAX) {

		const uint32_t index = freeHead_;
		freeHead_ = records_[index].nextFree;
		records_[index].nextFree = UINT32_MAX;
		return index;
	}
	if (records_.size() >= UINT32_MAX) {
		throw std::length_error("Entityの枠数が上限に達しました");
	}
	records_.emplace_back();
	return GetRecordCount() - 1;
}

void ECSWorld::RecycleIndex(uint32_t index) {

	EntityRecord& record = records_[index];
	record.alive = false;
	record.pendingDestroy = false;
	record.destroying = false;
	record.uuid = UUID{};
	record.location = {};
	// 世代を使い切った枠は再利用しない
	if (record.generation != UINT32_MAX) {
		++record.generation;
		record.nextFree = freeHead_;
		freeHead_ = index;
	}
}

uint64_t ECSWorld::ReserveComponentInstanceIDs(size_t count) {

	if (count > UINT64_MAX - nextComponentInstanceID_) {
		throw std::overflow_error("Component個体番号が上限に達しました");
	}
	const uint64_t first = nextComponentInstanceID_;
	nextComponentInstanceID_ += count;
	return first;
}

uint64_t ECSWorld::GetComponentInstanceID(const Entity& entity, uint32_t typeID) const {

	if (!IsAlive(entity)) {
		return 0;
	}
	const EntityLocation& location = records_[entity.index].location;
	if (!location.archetype->Has(typeID)) {
		return 0;
	}
	return location.archetype->GetChunks()[location.chunkIndex]->GetComponentInstanceID(
		location.archetype->GetColumnIndex(typeID), location.row);
}

ECSWorld::StructuralScope::StructuralScope(ECSWorld& world) : world_(world) {

	if (world_.queryDepth_ != 0 || world_.structuralChange_) {
		throw std::logic_error("走査中または構造変更中の追加と削除はCommandへ予約してください");
	}
	world_.structuralChange_ = true;
}

ECSWorld::StructuralScope::~StructuralScope() {

	world_.structuralChange_ = false;
}

ECSWorld::QueryScope::QueryScope(const ECSWorld& world) : world_(world) {

	if (world_.structuralChange_) {
		throw std::logic_error("構造変更途中のChunkを走査できません");
	}
	++world_.queryDepth_;
}

ECSWorld::QueryScope::~QueryScope() {

	--world_.queryDepth_;
}

void ECSWorld::MigrateEntity(const Entity& entity, const EntitySignature& oldSignature,
	const EntitySignature& newSignature, const PendingComponent* pending) {

	StructuralScope scope(*this);
	if (records_[entity.index].destroying) {
		throw std::logic_error("破棄通知中のEntity構成を変更できません");
	}
	const EntityLocation oldLocation = records_[entity.index].location;
	EntityArchetype* oldArchetype = oldLocation.archetype;
	EntityArchetype* newArchetype = GetOrCreateArchetype(newSignature);
	auto& registry = ComponentTypeRegistry::GetInstance();
	ComponentChangeChannel relocatedChannels = ComponentChangeChannel::None;
	size_t addedCount = 0;
	for (uint32_t typeID : newArchetype->GetTypes()) {
		if (!oldSignature.Test(typeID)) {
			++addedCount;
		}
	}
	uint64_t nextInstanceID = ReserveComponentInstanceIDs(addedCount);
	const auto [newChunkIndex, newRow] = newArchetype->AddUninitialized(entity);
	EntityChunk& newChunk = *newArchetype->GetChunks()[newChunkIndex];
	try {
		for (uint32_t typeID : newArchetype->GetTypes()) {

			// 失敗し得る新規構築を既存の値の移動より先に済ませる
			if (oldSignature.Test(typeID)) {
				continue;
			}
			if (pending && pending->GetInfo().id == typeID) {
				newChunk.CopyConstructByColumnIndex(newArchetype->GetColumnIndex(typeID), newRow,
					pending->GetData(), pending->GetInstanceID());
			} else {
				newArchetype->ConstructDefault(newChunkIndex, newRow, typeID, nextInstanceID++);
			}
			registry.GetInfo(typeID).initializeStorage(*this, entity, newArchetype->GetRaw(newChunkIndex, newRow, typeID));
		}
	} catch (...) {

		// 新規セルだけを解放し、旧Archetypeには触れない
		std::exception_ptr failure = std::current_exception();
		for (uint32_t typeID : newArchetype->GetTypes()) {
			const uint32_t column = newArchetype->GetColumnIndex(typeID);
			if (!oldSignature.Test(typeID) && newChunk.GetComponentInstanceID(column, newRow) != 0) {
				try {
					registry.GetInfo(typeID).releaseExternal(*this, entity, newChunk.GetRawByColumnIndex(column, newRow));
				} catch (...) {
					failure = std::current_exception();
				}
			}
		}
		newArchetype->RemoveSwap(newChunkIndex, newRow);
		std::rethrow_exception(failure);
	}

	EntityChunk& oldChunk = *oldArchetype->GetChunks()[oldLocation.chunkIndex];
	for (uint32_t typeID : newArchetype->GetTypes()) {
		if (!oldSignature.Test(typeID)) {
			continue;
		}

		// 保持Componentは個体番号と有効状態を引き継ぐ
		const auto& info = registry.GetInfo(typeID);
		const uint32_t oldColumn = oldArchetype->GetColumnIndex(typeID);
		const uint32_t newColumn = newArchetype->GetColumnIndex(typeID);
		newChunk.MoveConstructByColumnIndex(newColumn, newRow, oldChunk.GetRawByColumnIndex(oldColumn, oldLocation.row),
			oldChunk.GetComponentInstanceID(oldColumn, oldLocation.row));
		newChunk.SetEnabledByColumnIndex(newColumn, newRow, oldChunk.IsEnabledByColumnIndex(oldColumn, oldLocation.row));
		relocatedChannels |= info.changeChannels;
		++relocatedComponentCount_;
		relocatedComponentBytes_ += info.size;
	}
	std::exception_ptr failure;
	try {
		ReleaseExternalComponents(entity, &newSignature);
	} catch (...) {
		failure = std::current_exception();
	}

	// 移動後の行を公開し、旧行の空きを詰める
	const Entity moved = oldArchetype->RemoveSwap(oldLocation.chunkIndex, oldLocation.row);
	if (moved.IsValid()) {
		records_[moved.index].location = oldLocation;
	}
	records_[entity.index].location = { newArchetype, newChunkIndex, newRow };
	++structuralMigrationCount_;
	if (HasComponentChangeChannel(relocatedChannels, ComponentChangeChannel::Render)) {
		MarkRenderDataModified(entity);
	}
	if (HasComponentChangeChannel(relocatedChannels, ComponentChangeChannel::Lighting)) {
		changes_.MarkLightDataModified();
	}
	if (failure) {
		std::rethrow_exception(failure);
	}
}

void Engine::ECSWorld::ReleaseExternalComponents(const Entity& entity, const EntitySignature* retainedSignature) {

	if (!IsAlive(entity)) {
		return;
	}
	const EntityLocation location = records_[entity.index].location;
	EntityChunk& chunk = *location.archetype->GetChunks()[location.chunkIndex];
	std::exception_ptr failure;
	for (uint32_t typeID : location.archetype->GetTypes()) {

		const uint32_t column = location.archetype->GetColumnIndex(typeID);
		if ((retainedSignature && retainedSignature->Test(typeID)) || chunk.GetComponentInstanceID(column, location.row) == 0) {
			continue;
		}
		try {
			const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
			info.releaseExternal(*this, entity, chunk.GetRawByColumnIndex(column, location.row));
		} catch (...) {

			// 1件の解放失敗で残りの所有データを放置しない
			if (!failure) {
				failure = std::current_exception();
			}
		}
	}
	if (failure) {
		std::rethrow_exception(failure);
	}
}


void ECSWorld::CompleteComponentChange(const Entity& entity, uint32_t typeID, ComponentMutationKind kind) {

	const uint64_t instanceID = GetComponentInstanceID(entity, typeID);
	std::exception_ptr failure;
	try {
		const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
		if (kind == ComponentMutationKind::Added) {
			const EntityLocation location = records_[entity.index].location;
			info.onAdded(*this, entity, location.archetype->GetRaw(location.chunkIndex, location.row, typeID));
		} else {
			info.onRemoved(*this, entity);
		}
	} catch (...) {
		failure = std::current_exception();
	}
	try {

		// 追加処理中に破棄された個体へ追加通知を重ねない
		if (kind != ComponentMutationKind::Added || GetComponentInstanceID(entity, typeID) == instanceID) {
			NotifyComponentMutation(entity, typeID, kind);
		}
	} catch (...) {
		if (!failure) {
			failure = std::current_exception();
		}
	}
	if (failure) {
		std::rethrow_exception(failure);
	}
}

Entity ECSWorld::CreateEntityWithSignature(const EntitySignature& signature, UUID stableUUID) {

	const uint32_t componentTypeCount =
		ComponentTypeRegistry::GetInstance().GetComponentTypeCount();
	for (uint32_t typeID = 0; typeID < kMaxComponentTypes; ++typeID) {
		if (!signature.Test(typeID)) {
			continue;
		}
		if (typeID >= componentTypeCount) {
			throw std::invalid_argument("未登録のComponentType IDです");
		}
		ValidateComponentStorage(ComponentTypeRegistry::GetInstance().GetInfo(typeID));
	}
	return CreateEntityInArchetype(GetOrCreateArchetype(signature), stableUUID);
}

Entity ECSWorld::CreateEntityWithComponents(std::span<const uint32_t> typeIDs, UUID stableUUID) {

	EntitySignature signature{};
	const uint32_t componentTypeCount =
		ComponentTypeRegistry::GetInstance().GetComponentTypeCount();
	for (uint32_t typeID : typeIDs) {

		if (typeID >= componentTypeCount) {
			throw std::invalid_argument("未登録のComponentType IDです");
		}
		ValidateComponentStorage(ComponentTypeRegistry::GetInstance().GetInfo(typeID));
		signature.Set(typeID);
	}
	return CreateEntityWithSignature(signature, stableUUID);
}


void ECSWorld::ValidateComponentStorage(const ComponentTypeInfo& info) const {

	if (!CanStoreComponent(info)) {
		throw std::invalid_argument("ComponentTypeをこのWorldへ格納できません");
	}
}
