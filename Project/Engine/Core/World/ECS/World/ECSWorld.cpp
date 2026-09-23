#include "ECSWorld.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/World/ECS/World/ECSWorldSerialization.h>

// c++
#include <algorithm>

//============================================================================
//	ECSWorld classMethods
//============================================================================
ECSWorld::ECSWorld(ECSWorldKind kind) :
	kind_(kind) {

	// 最初は空のアーキタイプだけを作っておく
	EntitySignature empty{};
	emptyArchetype_ = GetOrCreateArchetype(empty);
}

ECSWorld::~ECSWorld() {

	for (uint32_t index = 0; index < records_.size(); ++index) {
		if (!records_[index].alive) {
			continue;
		}
		ReleaseExternalComponents(
			Entity{ index, records_[index].generation }, nullptr);
	}
	storage_.Clear();
}

Entity ECSWorld::CreateEntity(UUID stableUUID) {

	return CreateEntityInArchetype(emptyArchetype_, stableUUID);
}

Entity ECSWorld::CreateEntityWithSignature(const EntitySignature& signature, UUID stableUUID) {

	const uint32_t componentTypeCount =
		ComponentTypeRegistry::GetInstance().GetComponentTypeCount();
	for (uint32_t typeID = 0; typeID < componentTypeCount; ++typeID) {
		if (!signature.Test(typeID)) {
			continue;
		}
		Assert::Call(CanStoreComponent(
			ComponentTypeRegistry::GetInstance().GetInfo(typeID)),
			"ComponentTypeをこのWorldへ格納できません");
	}
	return CreateEntityInArchetype(GetOrCreateArchetype(signature), stableUUID);
}

Entity ECSWorld::CreateEntityWithComponents(std::span<const uint32_t> typeIDs, UUID stableUUID) {

	EntitySignature signature{};
	const uint32_t componentTypeCount =
		ComponentTypeRegistry::GetInstance().GetComponentTypeCount();
	for (uint32_t typeID : typeIDs) {

		Assert::Call(typeID < componentTypeCount, "未登録のComponentType IDです");
		Assert::Call(CanStoreComponent(
			ComponentTypeRegistry::GetInstance().GetInfo(typeID)),
			"ComponentTypeをこのWorldへ格納できません");
		signature.Set(typeID);
	}
	return CreateEntityWithSignature(signature, stableUUID);
}

Entity ECSWorld::CreateEntityInArchetype(EntityArchetype* archetype, UUID stableUUID) {

	Assert::Call(archetype != nullptr, "EntityArchetypeが必要です");

	// 空いているIDを割り当てる
	uint32_t index = AllocateIndex();
	Entity entity{ index, records_[index].generation };
	records_[index].alive = true;
	records_[index].pendingDestroy = false;
	records_[index].uuid = stableUUID ? stableUUID : UUID::New();

	// 最終Archetypeへ直接入れる
	auto [chunkIndex, row] = archetype->Add(entity);
	records_[index].location = EntityLocation{ archetype, chunkIndex, row };
	uuidToEntity_[records_[index].uuid] = entity;

	// チャンク外データを持つコンポーネントを初期化する
	const std::vector<uint32_t> initialTypes = archetype->GetTypes();
	for (uint32_t typeID : initialTypes) {

		const ComponentTypeInfo& info =
			ComponentTypeRegistry::GetInstance().GetInfo(typeID);
		const EntityLocation& current = records_[entity.index].location;
		void* ptr = current.archetype->GetRaw(
			current.chunkIndex, current.row, typeID);
		info.initializeStorage(*this, entity, ptr);
	}
	for (uint32_t typeID : initialTypes) {

		// OnAddedは関連Buffer追加で構造変更するため毎回現在位置を引き直す
		if (!HasComponent(entity, typeID)) {
			continue;
		}
		const ComponentTypeInfo& info =
			ComponentTypeRegistry::GetInstance().GetInfo(typeID);
		const EntityLocation& current = records_[entity.index].location;
		void* ptr = current.archetype->GetRaw(
			current.chunkIndex, current.row, typeID);
		info.onAdded(*this, entity, ptr);
	}
	MarkDataModified();
	const ComponentChangeChannel channels =
		GetChangeChannels(entity);
	if (HasComponentChangeChannel(
		channels, ComponentChangeChannel::Render)) {
		MarkRenderDataModified(entity);
	}
	if (HasComponentChangeChannel(
		channels, ComponentChangeChannel::Lighting)) {
		changes_.MarkLightDataModified();
	}
	return entity;
}

void ECSWorld::DestroyEntity(const Entity& entity) {

	// 存在しないエンティティは破棄できない
	if (!IsAlive(entity)) {
		return;
	}
	EntityRecord& record = records_[entity.index];
	if (record.pendingDestroy) {
		return;
	}

	// 走査中のチャンクを壊さないように、実破棄はフレーム終端でまとめて行う
	record.pendingDestroy = true;
	pendingDestroyEntities_.emplace_back(entity);
}

void ECSWorld::FlushPendingDestroyEntities() {

	if (pendingDestroyEntities_.empty()) {
		return;
	}

	// 破棄中に新しい破棄予約が追加されても、次回Flushへ回せるように一旦分離する
	std::vector<Entity> destroyingEntities{};
	destroyingEntities.swap(pendingDestroyEntities_);

	for (const Entity& entity : destroyingEntities) {
		if (!IsAlive(entity) || !records_[entity.index].pendingDestroy) {
			continue;
		}
		DestroyEntityImmediate(entity);
	}
}

void ECSWorld::DestroyEntityImmediate(const Entity& entity) {

	if (!IsAlive(entity)) {
		return;
	}

	EntityRecord& record = records_[entity.index];
	NotifyComponentMutation(entity, 0xFFFFFFFFu, ComponentMutationKind::EntityDestroyed);
	ReleaseExternalComponents(entity, nullptr);

	// アーキタイプから抜く
	Entity moved = record.location.archetype->RemoveSwap(
		record.location.chunkIndex, record.location.row);
	// 移動してきたエンティティが有効なら、レコードを更新する
	if (moved.IsValid()) {

		// 入れ替えで動いてきたエンティティの位置を更新
		records_[moved.index].location = record.location;
	}

	// 同じUUIDで再生成済みの場合に新しいマップを消さないよう、対象が一致する時だけ消す
	auto uuidIt = uuidToEntity_.find(record.uuid);
	if (uuidIt != uuidToEntity_.end() && uuidIt->second == entity) {

		uuidToEntity_.erase(uuidIt);
	}

	// レコードを無効化して再利用に回す
	record.alive = false;
	record.pendingDestroy = false;
	record.uuid = UUID{};
	record.location = EntityLocation{};
	// 世代をインクリメントして、古いIDが再利用されても区別できるようにする
	++record.generation;
	free_.emplace_back(entity.index);
}

bool Engine::ECSWorld::AddComponentByName(const Entity& entity, const std::string_view& typeName) {

	// エンティティが有効でなければ追加できない
	AssertAlive(entity);

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}
	Assert::Call(CanStoreComponent(*info),
		"ComponentTypeをこのWorldへ格納できません");

	// 既に持っているなら何もしない
	EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
	if (oldSignature.Test(info->id)) {
		return false;
	}

	// シグネチャを更新してアーキタイプを移動する
	EntitySignature newSignature = oldSignature;
	newSignature.Set(info->id);
	MigrateEntity(entity, oldSignature, newSignature);
	{
		const EntityLocation& current = records_[entity.index].location;
		void* ptr = current.archetype->GetRaw(
			current.chunkIndex, current.row, info->id);
		info->onAdded(*this, entity, ptr);
	}
	NotifyComponentMutation(entity, info->id, ComponentMutationKind::Added);
	return true;
}

bool Engine::ECSWorld::RemoveComponentByName(const Entity& entity, const std::string_view& typeName) {

	// エンティティが有効でなければ削除できない
	AssertAlive(entity);

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}

	// 持っていなければ何もしない
	EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
	if (!oldSignature.Test(info->id)) {
		return false;
	}

	// 新しいシグネチャをリセットしてアーキタイプを移動する
	EntitySignature newSignature = oldSignature;
	newSignature.Reset(info->id);
	MigrateEntity(entity, oldSignature, newSignature);
	// 本体削除後に関連BufferやRuntime Componentを連動して外す
	info->onRemoved(*this, entity);
	NotifyComponentMutation(entity, info->id, ComponentMutationKind::Removed);
	return true;
}

void ECSWorld::AddComponentFromJson(const Entity& entity, const std::string_view& typeName, const nlohmann::json& data) {

	ECSWorldSerialization::AddComponentFromJson(*this, entity, typeName, data);
}

bool Engine::ECSWorld::ApplyComponentJson(
	const Entity& entity, const std::string_view& typeName, const nlohmann::json& data) {

	return ECSWorldSerialization::ApplyComponentJson(*this, entity, typeName, data);
}

void Engine::ECSWorld::MarkComponentModified(const Entity& entity, uint32_t typeID) {

	if (!HasComponent(entity, typeID)) {
		return;
	}
	NotifyComponentMutation(entity, typeID, ComponentMutationKind::Modified);
}

void Engine::ECSWorld::MarkDataModified() {

	changes_.MarkDataModified();
}

void Engine::ECSWorld::MarkRenderDataModified() {

	changes_.MarkRenderDataModified();
}

void Engine::ECSWorld::MarkRenderDataModified(const Entity& entity) {

	changes_.MarkRenderDataModified(entity);
}

void Engine::ECSWorld::MarkMeshColorModified(const Entity& entity) {

	if (!IsAlive(entity)) { return; }
	changes_.MarkMeshColorModified(entity);
}

uint64_t Engine::ECSWorld::GetEntityRenderRevision(const Entity& entity) const {

	return changes_.GetEntityRenderRevision(entity);
}

uint64_t Engine::ECSWorld::GetMeshColorRevision(const Entity& entity) const {

	return changes_.GetMeshColorRevision(entity);
}

void Engine::ECSWorld::MarkTransformConsumersModified(
	ComponentChangeChannel channels,
	std::span<const Entity> changedTransforms) {

	changes_.MarkTransformConsumersModified(channels, changedTransforms);
}

bool Engine::ECSWorld::CollectRenderTransformChanges(
	uint64_t afterRevision, std::vector<Entity>& outEntities) const {

	return changes_.CollectRenderTransformChanges(afterRevision, outEntities);
}

ComponentChangeChannel Engine::ECSWorld::GetTransformChangeChannels(
	const Entity& entity) const {

	if (!IsAlive(entity)) {
		return ComponentChangeChannel::None;
	}

	ComponentChangeChannel channels =
		ComponentChangeChannel::None;
	const EntityArchetype* archetype =
		records_[entity.index].location.archetype;
	for (uint32_t typeID : archetype->GetTypes()) {
		channels |= ComponentTypeRegistry::GetInstance().
			GetInfo(typeID).transformChannels;
	}
	return channels;
}

bool Engine::ECSWorld::CanStoreComponent(const ComponentTypeInfo& info) const {

	if (info.worldDomain == ComponentWorldDomain::Both) {
		return true;
	}
	if (kind_ == ECSWorldKind::Authoring) {
		return info.worldDomain == ComponentWorldDomain::Authoring;
	}
	return info.worldDomain == ComponentWorldDomain::Runtime;
}

uint64_t Engine::ECSWorld::AddComponentMutationListener(
	ComponentMutationCallback callback, void* userData) {

	return changes_.AddComponentMutationListener(callback, userData);
}

void Engine::ECSWorld::RemoveComponentMutationListener(uint64_t listenerID) {

	changes_.RemoveComponentMutationListener(listenerID);
}

void Engine::ECSWorld::NotifyComponentMutation(
	const Entity& entity, uint32_t typeID, ComponentMutationKind kind) {

	MarkDataModified();
	ComponentChangeChannel channels =
		ComponentChangeChannel::None;
	const uint32_t componentTypeCount =
		ComponentTypeRegistry::GetInstance().GetComponentTypeCount();
	if (typeID < componentTypeCount) {
		channels = ComponentTypeRegistry::GetInstance().
			GetInfo(typeID).changeChannels;
	} else if (kind == ComponentMutationKind::EntityDestroyed) {
		channels = GetChangeChannels(entity);
	}
	changes_.Notify(*this, entity, typeID, kind, channels);
}

ComponentChangeChannel Engine::ECSWorld::GetChangeChannels(
	const Entity& entity) const {

	if (!IsAlive(entity)) {
		return ComponentChangeChannel::None;
	}

	ComponentChangeChannel channels =
		ComponentChangeChannel::None;
	const EntityArchetype* archetype =
		records_[entity.index].location.archetype;
	for (uint32_t typeID : archetype->GetTypes()) {
		channels |= ComponentTypeRegistry::GetInstance().
			GetInfo(typeID).changeChannels;
	}
	return channels;
}

std::unique_ptr<Engine::ECSWorld>
Engine::ECSWorld::CloneForSerialization() const {

	return ECSWorldSerialization::CloneForSerialization(*this);
}

void ECSWorld::SerializeEntityComponents(const Entity& entity, nlohmann::json& outComponents) const {

	ECSWorldSerialization::SerializeEntityComponents(*this, entity, outComponents);
}

bool Engine::ECSWorld::SerializeComponentToJson(const Entity& entity, const std::string_view& typeName, nlohmann::json& outData) const {

	return ECSWorldSerialization::SerializeComponentToJson(*this, entity, typeName, outData);
}

bool ECSWorld::IsAlive(const Entity& entity) const {

	// エンティティが有効かどうか
	if (!entity.IsValid() || GetRecordCount() <= entity.index) {
		return false;
	}
	const auto& record = records_[entity.index];
	return record.alive && record.generation == entity.generation;
}

bool ECSWorld::IsPendingDestroy(const Entity& entity) const {

	if (!IsAlive(entity)) {
		return false;
	}
	return records_[entity.index].pendingDestroy;
}

Engine::UUID ECSWorld::GetUUID(const Entity& entity) const {

	// エンティティが有効かどうか
	if (!IsAlive(entity)) {
		return UUID{};
	}
	return records_[entity.index].uuid;
}

Entity ECSWorld::FindByUUID(UUID id) const {

	auto it = uuidToEntity_.find(id);
	return (it == uuidToEntity_.end()) ? Entity::Null() : it->second;
}

Engine::ECSWorldStatistics Engine::ECSWorld::GetStatistics() const {

	ECSWorldStatistics statistics{};
	statistics.recordCount = GetRecordCount();
	statistics.archetypeCount = GetArchetypeCount();
	statistics.structuralMigrationCount = structuralMigrationCount_;
	statistics.relocatedComponentCount = relocatedComponentCount_;
	statistics.relocatedComponentBytes = relocatedComponentBytes_;

	for (const EntityRecord& record : records_) {
		statistics.aliveEntityCount += record.alive ? 1u : 0u;
	}
	for (const auto& [signature, archetype] : archetypes_) {

		statistics.chunkSlotCount += archetype->GetChunkCount();
		statistics.allocatedChunkCount += archetype->GetAllocatedChunkCount();
		statistics.allocatedChunkBytes += archetype->GetAllocatedBytes();
		statistics.payloadBytes += archetype->GetPayloadBytes();
	}
	return statistics;
}

void Engine::ECSWorld::ResetFrameStatistics() {

	structuralMigrationCount_ = 0;
	relocatedComponentCount_ = 0;
	relocatedComponentBytes_ = 0;
}

bool Engine::ECSWorld::HasComponent(const Entity& entity, uint32_t typeID) const {

	if (!IsAlive(entity) || ComponentTypeRegistry::GetInstance().GetComponentTypeCount() <= typeID) {
		return false;
	}
	return records_[entity.index].location.archetype->Has(typeID);
}

bool Engine::ECSWorld::HasComponent(const Entity& entity, const std::string_view& typeName) const {

	// エンティティが有効かどうか
	if (!IsAlive(entity)) {
		return false;
	}

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}
	return records_[entity.index].location.archetype->Has(info->id);
}

const Engine::ComponentTypeInfo* Engine::ECSWorld::FindBufferType(const Entity& entity, uint32_t typeID) const {

	if (!IsAlive(entity)) {
		return nullptr;
	}
	ComponentTypeRegistry& registry =
		ComponentTypeRegistry::GetInstance();
	if (typeID >= registry.GetComponentTypeCount()) {
		return nullptr;
	}
	const ComponentTypeInfo& info = registry.GetInfo(typeID);
	if (info.storageKind != ComponentStorageKind::Buffer ||
		!HasComponent(entity, typeID)) {
		return nullptr;
	}

	return &info;
}

Engine::UntypedDynamicBuffer Engine::ECSWorld::TryGetUntypedBuffer(const Entity& entity, uint32_t typeID) {

	const ComponentTypeInfo* info = FindBufferType(entity, typeID);
	if (!info) {
		return {};
	}
	EntityLocation& location = records_[entity.index].location;
	void* storage = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);
	return UntypedDynamicBuffer(static_cast<DynamicBufferHeader*>(storage),
		info->elementSize, info->elementAlign, info->bufferElementTriviallyCopyable);
}

Engine::ReadOnlyUntypedDynamicBuffer Engine::ECSWorld::TryGetUntypedBuffer(const Entity& entity, uint32_t typeID) const {

	const ComponentTypeInfo* info = FindBufferType(entity, typeID);
	if (!info) {
		return {};
	}
	const EntityLocation& location = records_[entity.index].location;
	const EntityArchetype& archetype = *location.archetype;
	const void* storage = archetype.GetRaw(location.chunkIndex, location.row, typeID);
	return ReadOnlyUntypedDynamicBuffer(static_cast<const DynamicBufferHeader*>(storage),
		info->elementSize, info->elementAlign, info->bufferElementTriviallyCopyable);
}

uint32_t ECSWorld::AllocateIndex() {

	// 破棄されたエンティティIDがあれば再利用する、なければ新しいIDを作る
	if (!free_.empty()) {

		uint32_t index = free_.back();
		free_.pop_back();
		return index;
	}
	// 新しいIDを作る
	records_.push_back(EntityRecord{});
	return GetRecordCount() - 1;
}

void ECSWorld::AssertAlive(const Entity& entity) const {

	Assert::Call(IsAlive(entity), "Entityが無効または破棄済みです");
}

void ECSWorld::MigrateEntity(const Entity& entity, const EntitySignature& oldSignature, const EntitySignature& newSignature) {

	// シグネチャからアーキタイプを取得
	EntityArchetype* oldArchetype = records_[entity.index].location.archetype;
	EntityArchetype* newArchetype = GetOrCreateArchetype(newSignature);
	ComponentChangeChannel relocatedChannels = ComponentChangeChannel::None;
	for (uint32_t typeID : oldArchetype->GetTypes()) {
		if (newSignature.Test(typeID)) {
			relocatedChannels |= ComponentTypeRegistry::GetInstance().
				GetInfo(typeID).changeChannels;
		}
	}

	// 新アーキタイプへ未構築の行として追加する
	auto [newChunkIndex, newRow] = newArchetype->AddUninitialized(entity);
	auto& oldLocation = records_[entity.index].location;

	const auto& newTypes = newArchetype->GetTypes();
	for (auto typeID : newTypes) {

		const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
		void* dst = newArchetype->GetRaw(newChunkIndex, newRow, typeID);
		if (oldSignature.Test(typeID)) {

			const uint32_t oldColumnIndex = oldArchetype->GetColumnIndex(typeID);
			const bool componentEnabled =
				oldArchetype->GetChunks()[oldLocation.chunkIndex]->IsEnabledByColumnIndex(
					oldColumnIndex, oldLocation.row);
			void* src = oldArchetype->GetRaw(oldLocation.chunkIndex, oldLocation.row, typeID);
			info.moveConstruct(dst, src);
			// Enableable状態もデータと同じ行へ移し、構造変更で有効状態を失わない
			const uint32_t newColumnIndex = newArchetype->GetColumnIndex(typeID);
			newArchetype->GetChunks()[newChunkIndex]->SetEnabledByColumnIndex(
				newColumnIndex, newRow, componentEnabled);
			++relocatedComponentCount_;
			relocatedComponentBytes_ += info.size;
		} else {

			// 新規追加コンポーネントだけデフォルト構築
			newArchetype->ConstructDefault(newChunkIndex, newRow, typeID);
			info.initializeStorage(*this, entity, dst);
		}
	}
	++structuralMigrationCount_;
	ReleaseExternalComponents(entity, &newSignature);

	// 古いアーキタイプから対象エンティティを抜く
	Entity moved = oldArchetype->RemoveSwap(oldLocation.chunkIndex, oldLocation.row);
	if (moved.IsValid()) {

		records_[moved.index].location = oldLocation;
	}
	// 対象エンティティの新位置を記録
	records_[entity.index].location = EntityLocation{ newArchetype, newChunkIndex, newRow };

	// 構造移動で保持Componentのアドレスが変わるため、外部キャッシュを再抽出する
	if (HasComponentChangeChannel(
		relocatedChannels, ComponentChangeChannel::Render)) {
		MarkRenderDataModified(entity);
	}
	if (HasComponentChangeChannel(
		relocatedChannels, ComponentChangeChannel::Lighting)) {
		changes_.MarkLightDataModified();
	}
}

void Engine::ECSWorld::ReleaseExternalComponents(
	const Entity& entity, const EntitySignature* retainedSignature) {

	if (!IsAlive(entity)) {
		return;
	}

	const EntityLocation& location = records_[entity.index].location;
	for (uint32_t typeID : location.archetype->GetTypes()) {

		if (retainedSignature && retainedSignature->Test(typeID)) {
			continue;
		}
		const ComponentTypeInfo& info =
			ComponentTypeRegistry::GetInstance().GetInfo(typeID);
		void* ptr = location.archetype->GetRaw(
			location.chunkIndex, location.row, typeID);
		info.releaseExternal(*this, entity, ptr);
	}
}

EntityArchetype* ECSWorld::GetOrCreateArchetype(const EntitySignature& signature) {

	// すでにあるならそれを返す
	auto it = archetypes_.find(signature);
	if (it != archetypes_.end()) {
		return it->second.get();
	}

	// シグネチャからアーキタイプを作る
	std::vector<uint32_t> types;
	types.reserve(16);

	// レジストリに登録済み範囲だけ
	uint32_t count = ComponentTypeRegistry::GetInstance().GetComponentTypeCount();
	for (uint32_t typeIndex = 0; typeIndex < count; ++typeIndex) {
		if (signature.Test(typeIndex)) {

			types.push_back(typeIndex);
		}
	}

	// 新しいアーキタイプを作る
	auto archetype = std::make_unique<EntityArchetype>(signature, std::move(types));
	EntityArchetype* raw = archetype.get();
	archetypes_.emplace(signature, std::move(archetype));
	// archetypeが増えたのでForEachのmatchPlanを無効化するためversionを進める
	++archetypeVersion_;
	return raw;
}
