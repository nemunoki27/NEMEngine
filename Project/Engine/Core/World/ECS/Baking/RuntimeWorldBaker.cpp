#include "RuntimeWorldBaker.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

//============================================================================
//	RuntimeWorldBaker classMethods
//============================================================================
Engine::RuntimeWorldBaker::~RuntimeWorldBaker() {

	Detach();
}

void Engine::RuntimeWorldBaker::Attach(
	ECSWorld& world, AssetDatabase* assetDatabase) {

	Detach();
	if (world.GetKind() != ECSWorldKind::Runtime) {
		return;
	}

	// 購読が成立してから接続先を公開する
	mutationListenerID_ = world.AddComponentMutationListener(&RuntimeWorldBaker::OnComponentMutation, this);
	world_ = &world;
	worldLifetime_ = world.GetLifetime();
	assetDatabase_ = assetDatabase;
}

void Engine::RuntimeWorldBaker::Detach() {

	if (IsAttached() && mutationListenerID_ != 0) {
		world_->RemoveComponentMutationListener(mutationListenerID_);
	}
	world_ = nullptr;
	worldLifetime_.reset();
	assetDatabase_ = nullptr;
	mutationListenerID_ = 0;
	dirtyEntities_.clear();
	dirtyEntityKeys_.clear();
	baking_ = false;
}

void Engine::RuntimeWorldBaker::BakeAll() {

	if (!IsAttached() || baking_) {
		return;
	}

	// 全体変換も差分と同じ待機列へ積む
	world_->ForEachAliveEntity([this](const Entity& entity) {
		MarkDirty(entity);
	});
	Flush();
}

void Engine::RuntimeWorldBaker::Flush() {

	if (!IsAttached() || baking_) {
		return;
	}

	baking_ = true;
	const uint64_t listenerID = mutationListenerID_;
	try {
		while (!dirtyEntities_.empty()) {

			// 変換に成功した対象だけ待機列から外す
			const Entity entity = dirtyEntities_.front();
			BakeEntity(entity);
			if (listenerID != mutationListenerID_ || !IsAttached()) {
				baking_ = false;
				return;
			}
			dirtyEntityKeys_.erase(MakeEntityKey(entity));
			dirtyEntities_.pop_front();
		}
	} catch (...) {
		baking_ = false;
		throw;
	}
	baking_ = false;
}

void Engine::RuntimeWorldBaker::OnComponentMutation(
	[[maybe_unused]] ECSWorld& world, const Entity& entity,
	uint32_t typeID, ComponentMutationKind kind, void* userData) {

	auto* baker = static_cast<RuntimeWorldBaker*>(userData);
	if (!baker || baker->baking_ ||
		kind == ComponentMutationKind::EntityDestroyed ||
		!IsBakeRelevant(typeID)) {
		return;
	}
	baker->MarkDirty(entity);
}

bool Engine::RuntimeWorldBaker::IsBakeRelevant(uint32_t typeID) {

	ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
	return typeID == registry.GetID<MeshRendererComponent>();
}

void Engine::RuntimeWorldBaker::MarkDirty(const Entity& entity) {

	const uint64_t key = MakeEntityKey(entity);
	if (!dirtyEntityKeys_.emplace(key).second) {
		return;
	}
	try {
		dirtyEntities_.emplace_back(entity);
	} catch (...) {
		dirtyEntityKeys_.erase(key);
		throw;
	}
}

void Engine::RuntimeWorldBaker::BakeEntity(const Entity& entity) {

	if (!IsAttached() || !world_->IsAlive(entity)) {
		return;
	}

	if (assetDatabase_ && world_->HasComponent<MeshRendererComponent>(entity)) {

		// Meshレイアウト変更を安定IDで追従し、描画抽出前にサブメッシュ列を揃える
		MeshSubMeshAuthoring::SyncEntity(
			assetDatabase_, *world_, entity, true);
	}
}

uint64_t Engine::RuntimeWorldBaker::MakeEntityKey(const Entity& entity) {

	return static_cast<uint64_t>(entity.generation) << 32 |
		static_cast<uint64_t>(entity.index);
}
