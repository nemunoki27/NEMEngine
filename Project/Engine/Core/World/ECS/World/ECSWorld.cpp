#include "ECSWorld.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/ECS/World/ECSWorldSerialization.h>

// c++
#include <algorithm>
#include <exception>
#include <stdexcept>

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

	// 外部登録から破棄中のWorldを参照させない
	lifetime_->alive_ = false;
	commandBuffer_.Clear();
	structuralChange_ = true;

	for (uint32_t index = 0; index < records_.size(); ++index) {
		if (!records_[index].alive) {
			continue;
		}
		try {
			ReleaseExternalComponents(Entity{ index, records_[index].generation }, nullptr);
		} catch (...) {
			// 解放失敗を記録し、残りのEntityの終了を続ける
			try {
				Logger::Output(LogType::Engine, spdlog::level::err, "World終了時のComponent解放に失敗しました Entity={}", index);
			} catch (...) {
				// 診断の失敗をデストラクタの外へ出さない
			}
		}
	}
	storage_.Clear();
}

Entity ECSWorld::CreateEntity(UUID stableUUID) {

	return CreateEntityInArchetype(emptyArchetype_, stableUUID);
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
	pendingDestroyEntities_.emplace_back(entity);
	record.pendingDestroy = true;
}

void ECSWorld::FlushPendingDestroyEntities() {

	if (queryDepth_ != 0 || structuralChange_ || flushingDestroy_ || pendingDestroyEntities_.empty()) {
		return;
	}

	flushingDestroy_ = true;
	const size_t count = pendingDestroyEntities_.size();
	try {
		for (size_t index = 0; index < count; ++index) {

			// 通知中の新しい予約は次の安全地点へ回す
			const Entity entity = pendingDestroyEntities_.front();
			pendingDestroyEntities_.pop_front();
			if (IsAlive(entity) && records_[entity.index].pendingDestroy) {
				DestroyEntityImmediate(entity);
			}
		}
	} catch (...) {
		flushingDestroy_ = false;
		throw;
	}
	flushingDestroy_ = false;
}

bool Engine::ECSWorld::AddComponentByName(const Entity& entity, const std::string_view& typeName) {

	// エンティティが有効でなければ追加できない
	AssertAlive(entity);

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}
	ValidateComponentStorage(*info);

	// 既に持っているなら何もしない
	EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
	if (oldSignature.Test(info->id)) {
		return false;
	}

	// シグネチャを更新してアーキタイプを移動する
	EntitySignature newSignature = oldSignature;
	newSignature.Set(info->id);
	MigrateEntity(entity, oldSignature, newSignature);
	CompleteComponentChange(entity, info->id, ComponentMutationKind::Added);
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
	CompleteComponentChange(entity, info->id, ComponentMutationKind::Removed);
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

void ECSWorld::AssertAlive(const Entity& entity) const {

	if (!IsAlive(entity)) {
		throw std::invalid_argument("Entityが無効または破棄済みです");
	}
}

EntityArchetype* ECSWorld::GetOrCreateArchetype(const EntitySignature& signature) {

	if (queryDepth_ != 0) {
		throw std::logic_error("走査中にArchetypeを追加できません");
	}

	// すでにあるならそれを返す
	auto it = archetypes_.find(signature);
	if (it != archetypes_.end()) {
		return it->second.get();
	}

	// シグネチャからアーキタイプを作る
	if (archetypeVersion_ == UINT32_MAX - 1) {
		throw std::overflow_error("Archetypeの世代が上限に達しました");
	}
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
