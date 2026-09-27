#include "WorldCommandBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <stdexcept>

//============================================================================
//	WorldCommandBuffer classMethods
//============================================================================
uint64_t Engine::WorldCommandBuffer::StageAddComponent(ECSWorld& world, const Entity& entity, uint32_t typeID) {

	if (&world.GetCommandBuffer() != this) {
		throw std::invalid_argument("追加予約のWorldが一致しません");
	}
	if (!world.IsAlive(entity) || world.IsPendingDestroy(entity)) {
		return 0;
	}
	const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
	world.ValidateComponentStorage(info);
	if (info.size == 0) {
		throw std::invalid_argument("このComponentは追加予約できません");
	}
	if (const uint64_t existing = world.GetComponentInstanceID(entity, typeID)) {
		return existing;
	}
	if (const PendingComponent* pending = FindPendingComponent(entity, typeID)) {
		return pending->GetInstanceID();
	}

	// 構築に成功した値を予約と取得索引で共有する
	auto component = std::make_shared<PendingComponent>(info, world.ReserveComponentInstanceIDs(1));
	WorldCommand command{};
	command.kind = WorldCommandKind::AddComponentValue;
	command.target = entity;
	command.component = component;
	const ComponentKey key{ entity.index, entity.generation, typeID };
	const auto [entry, inserted] = pendingComponents_.emplace(key, component);
	if (!inserted) {
		return entry->second->GetInstanceID();
	}
	try {
		commands_.emplace_back(std::move(command));
	} catch (...) {
		pendingComponents_.erase(entry);
		throw;
	}
	return component->GetInstanceID();
}

Engine::PendingComponent* Engine::WorldCommandBuffer::FindPendingComponent(const Entity& entity, uint32_t typeID) {

	const auto entry = pendingComponents_.find({ entity.index, entity.generation, typeID });
	return entry != pendingComponents_.end() ? entry->second.get() : nullptr;
}

const Engine::PendingComponent* Engine::WorldCommandBuffer::FindPendingComponent(const Entity& entity, uint32_t typeID) const {

	const auto entry = pendingComponents_.find({ entity.index, entity.generation, typeID });
	return entry != pendingComponents_.end() ? entry->second.get() : nullptr;
}

void Engine::WorldCommandBuffer::CollectPendingComponents(const Entity& entity, std::vector<const PendingComponent*>& out) const {

	out.clear();
	// Entityの世代が一致する範囲だけを走査する
	for (auto it = pendingComponents_.lower_bound({ entity.index, entity.generation, 0 }); it != pendingComponents_.end(); ++it) {
		if (std::get<0>(it->first) != entity.index || std::get<1>(it->first) != entity.generation) break;
		if (it->second->GetInstanceID() != 0) out.push_back(it->second.get());
	}
}

void Engine::WorldCommandBuffer::CollectUnappliedCommands(std::vector<WorldCommand>& out) const {

	out.clear();
	// 失敗したFlushの残りを先に保存する
	if (activeCommandIndex_ < activeBatch_.size()) {
		out.insert(out.end(), activeBatch_.begin() + activeCommandIndex_, activeBatch_.end());
	}
	// 次回Flush待ちのCommandを後ろへ続ける
	out.insert(out.end(), commands_.begin(), commands_.end());
}

void Engine::WorldCommandBuffer::CancelPendingComponent(const Entity& entity, uint32_t typeID) {

	const auto entry = pendingComponents_.find({ entity.index, entity.generation, typeID });
	if (entry == pendingComponents_.end()) {
		return;
	}
	auto cancelled = std::move(entry->second);
	pendingComponents_.erase(entry);
	cancelled->Cancel();
}

void Engine::WorldCommandBuffer::RemovePendingComponent(const WorldCommand& command) {

	if (command.kind != WorldCommandKind::AddComponentValue) {
		return;
	}
	// 適用失敗や対象失効でも予約を残さない
	const ComponentKey key{ command.target.index, command.target.generation, command.component->GetInfo().id };
	const auto entry = pendingComponents_.find(key);
	if (entry != pendingComponents_.end() && entry->second == command.component) {
		pendingComponents_.erase(entry);
	}
}

void Engine::WorldCommandBuffer::EnqueueCreateEntity(ECSWorld& world, const Entity& reserved,
	std::string_view name, const Entity& parent) {

	if (!world.IsAlive(reserved) || world.IsPendingDestroy(reserved)) {
		throw std::invalid_argument("生成対象のEntityが無効です");
	}

	// 初期Componentも追加予約と同じ個体番号で保持する
	auto& registry = ComponentTypeRegistry::GetInstance();
	StageAddComponent(world, reserved, registry.GetID<TransformComponent>());
	StageAddComponent(world, reserved, registry.GetID<HierarchyComponent>());
	StageAddComponent(world, reserved, registry.GetID<NameComponent>());
	StageAddComponent(world, reserved, registry.GetID<SceneObjectComponent>());
	world.TryGetComponentForBinding<NameComponent>(reserved)->name = name.empty() ? "GameObject" : std::string(name);

	// 生成直後の保存参照にも同じIDと所属を返す
	auto& sceneObject = *world.TryGetComponentForBinding<SceneObjectComponent>(reserved);
	if (!sceneObject.localFileID) {
		sceneObject.localFileID = UUID::New();
	}
	if (const auto* parentObject = world.TryGetComponentForBinding<SceneObjectComponent>(parent)) {
		sceneObject.sceneInstanceID = parentObject->sceneInstanceID;
		sceneObject.sourceAsset = parentObject->sourceAsset;
	} else if (const auto* scenes = world.GetCommandServices().sceneInstances) {
		if (const auto* active = scenes->GetActive()) {
			sceneObject.sceneInstanceID = active->instanceID;
			sceneObject.sourceAsset = active->sceneAsset;
		}
	}

	// 親子の実体リンクは安全地点で確定する
	WorldCommand command{};
	command.kind = WorldCommandKind::CreateEntity;
	command.target = reserved;
	command.parent = parent;
	commands_.emplace_back(std::move(command));
}
