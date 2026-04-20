#include "ECSWorld.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>

//============================================================================
//	ECSWorld classMethods
//============================================================================

ECSWorld::ECSWorld() {

	// 最初は空のアーキタイプだけを作っておく
	EntitySignature empty{};
	emptyArchetype_ = GetOrCreateArchetype(empty);
}

Entity ECSWorld::CreateEntity(UUID stableUUID) {

	// 空いているIDを割り当てる
	uint32_t index = AllocateIndex();
	Entity entity{ index, records_[index].generation };
	records_[index].alive = true;
	records_[index].uuid = stableUUID ? stableUUID : UUID::New();

	// 空アーキタイプへ入れる
	auto [chunkIndex, row] = emptyArchetype_->Add(entity);
	records_[index].location = EntityLocation{ emptyArchetype_, chunkIndex, row };
	uuidToEntity_[records_[index].uuid] = entity;
	return entity;
}

void ECSWorld::DestroyEntity(const Entity& entity) {

	// 存在しないエンティティは破棄できない
	if (!IsAlive(entity)) {
		return;
	}

	// アーキタイプから抜く
	Entity moved = records_[entity.index].location.archetype->RemoveSwap(
		records_[entity.index].location.chunkIndex, records_[entity.index].location.row);
	// 移動してきたエンティティが有効なら、レコードを更新する
	if (moved.IsValid()) {

		// 入れ替えで動いてきたエンティティの位置を更新
		records_[moved.index].location = records_[entity.index].location;
	}

	// マップからも消す
	uuidToEntity_.erase(records_[entity.index].uuid);

	// レコードを無効化して再利用に回す
	records_[entity.index].alive = false;
	// 世代をインクリメントして、古いIDが再利用されても区別できるようにする
	++records_[entity.index].generation;
	free_.emplace_back(entity.index);
}

bool Engine::ECSWorld::AddComponentByName(const Entity& entity, const std::string_view& typeName) {

	// エンティティが有効でなければ追加できない
	AssertAlive(entity);

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}

	// 既に持っているなら何もしない
	EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
	if (oldSignature.Test(info->id)) {
		return false;
	}

	// シグネチャを更新してアーキタイプを移動する
	EntitySignature newSignature = oldSignature;
	newSignature.Set(info->id);
	MigrateEntity(entity, oldSignature, newSignature);
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
	return true;
}

void ECSWorld::AddComponentFromJson(const Entity& entity, const std::string_view& typeName, const nlohmann::json& data) {

	// エンティティが有効でなければ追加できない
	AssertAlive(entity);

	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	Assert::Call(info, "Unknown component typeName in scene file");

	// 既に持っているなら上書きする
	if (!records_[entity.index].location.archetype->Has(info->id)) {

		// シグネチャを更新してアーキタイプを移動する
		EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
		EntitySignature newSignature = oldSignature;
		newSignature.Set(info->id);
		// 新しいアーキタイプへ移動する
		MigrateEntity(entity, oldSignature, newSignature);
	}

	// 追加/移動後のメモリにjsonを渡す
	auto& location = records_[entity.index].location;
	void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, info->id);
	info->from_json(ptr, data);
}

void ECSWorld::SerializeEntityComponents(const Entity& entity, nlohmann::json& outComponents) const {

	Assert::Call(IsAlive(entity), "entity is not alive");

	// アーキタイプから持っているコンポーネントの種類を取得
	const EntityArchetype* archetype = records_[entity.index].location.archetype;
	const auto& types = archetype->GetTypes();

	outComponents = nlohmann::json::object();
	for (auto typeID : types) {

		const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);

		// アーキタイプからコンポーネントデータを取得
		auto& location = records_[entity.index].location;
		void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);

		// jsonに変換して出力
		info.to_json(ptr, outComponents[info.name]);
	}
}

bool Engine::ECSWorld::SerializeComponentToJson(const Entity& entity, const std::string_view& typeName, nlohmann::json& outData) const {

	// エンティティが有効でなければシリアライズできない
	if (!IsAlive(entity)) {
		return false;
	}

	// コンポーネントの種類IDを取得
	const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (!info) {
		return false;
	}

	const auto& location = records_[entity.index].location;
	if (!location.archetype->Has(info->id)) {
		return false;
	}
	// アーキタイプからコンポーネントデータを取得
	void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, info->id);
	info->to_json(ptr, outData);
	return true;
}

bool ECSWorld::IsAlive(const Entity& entity) const {

	// エンティティが有効かどうか
	if (!entity.IsValid() || GetRecordCount() <= entity.index) {
		return false;
	}
	const auto& record = records_[entity.index];
	return record.alive && record.generation == entity.generation;
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

uint32_t ECSWorld::AllocateIndex() {

	// 破棄されたエンティティIDがあれば再利用する。なければ新しいIDを作る
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

	Assert::Call(IsAlive(entity), "Entity is not alive / invalid handle");
}

void ECSWorld::MigrateEntity(const Entity& entity, const EntitySignature& oldSignature, const EntitySignature& newSignature) {

	// シグネチャからアーキタイプを取得
	EntityArchetype* oldArchetype = records_[entity.index].location.archetype;
	EntityArchetype* newArchetype = GetOrCreateArchetype(newSignature);

	// 新アーキタイプへ未構築の行として追加する
	auto [newChunkIndex, newRow] = newArchetype->AddUninitialized(entity);
	auto& oldLocation = records_[entity.index].location;

	const auto& newTypes = newArchetype->GetTypes();
	for (auto typeID : newTypes) {

		const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
		void* dst = newArchetype->GetRaw(newChunkIndex, newRow, typeID);
		if (oldSignature.Test(typeID)) {

			void* src = oldArchetype->GetRaw(oldLocation.chunkIndex, oldLocation.row, typeID);
			info.moveConstruct(dst, src);
		} else {

			// 新規追加コンポーネントだけデフォルト構築
			newArchetype->ConstructDefault(newChunkIndex, newRow, typeID);
		}
	}

	// 古いアーキタイプから対象エンティティを抜く
	Entity moved = oldArchetype->RemoveSwap(oldLocation.chunkIndex, oldLocation.row);
	if (moved.IsValid()) {

		records_[moved.index].location = oldLocation;
	}
	// 対象エンティティの新位置を記録
	records_[entity.index].location = EntityLocation{ newArchetype, newChunkIndex, newRow };
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
	return raw;
}