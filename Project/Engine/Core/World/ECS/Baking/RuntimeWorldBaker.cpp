#include "RuntimeWorldBaker.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
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

	world_ = &world;
	assetDatabase_ = assetDatabase;
	mutationListenerID_ = world_->AddComponentMutationListener(
		&RuntimeWorldBaker::OnComponentMutation, this);
}

void Engine::RuntimeWorldBaker::Detach() {

	if (world_ && mutationListenerID_ != 0) {
		world_->RemoveComponentMutationListener(mutationListenerID_);
	}
	world_ = nullptr;
	assetDatabase_ = nullptr;
	mutationListenerID_ = 0;
	dirtyEntities_.clear();
	dirtyEntityKeys_.clear();
	baking_ = false;
}

void Engine::RuntimeWorldBaker::BakeAll() {

	if (!world_) {
		return;
	}

	// 初回は全Entityを一巡し、最初のSystem更新前に派生データを完成させる
	baking_ = true;
	world_->ForEachAliveEntity([this](const Entity& entity) {
		BakeEntity(entity);
		});
	baking_ = false;
	dirtyEntities_.clear();
	dirtyEntityKeys_.clear();
}

void Engine::RuntimeWorldBaker::Flush() {

	if (!world_ || dirtyEntities_.empty()) {
		return;
	}

	// 通知中に積まれる次回分と分離して安全に差分を処理する
	std::vector<Entity> entities{};
	entities.swap(dirtyEntities_);
	dirtyEntityKeys_.clear();

	baking_ = true;
	for (const Entity& entity : entities) {
		BakeEntity(entity);
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
	return typeID == registry.GetID<CollisionComponent>() ||
		typeID == registry.GetID<CollisionShape>() ||
		typeID == registry.GetID<MeshRendererComponent>();
}

void Engine::RuntimeWorldBaker::MarkDirty(const Entity& entity) {

	const uint64_t key = MakeEntityKey(entity);
	if (!dirtyEntityKeys_.emplace(key).second) {
		return;
	}
	dirtyEntities_.emplace_back(entity);
}

void Engine::RuntimeWorldBaker::BakeEntity(const Entity& entity) {

	if (!world_ || !world_->IsAlive(entity)) {
		return;
	}

	if (world_->HasComponent<CollisionComponent>(entity)) {

		// Collider設定を判定用の共有Blobへ変換する
		EnsureCollisionShapes(*world_, entity);
		if (!world_->HasComponent<CollisionRuntimeStateComponent>(entity)) {
			world_->AddComponent<CollisionRuntimeStateComponent>(entity);
		}
		if (!world_->HasComponent<CollisionCompoundComponent>(entity)) {
			world_->AddComponent<CollisionCompoundComponent>(entity);
		}
		RebuildCollisionCompound(*world_, entity);
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
