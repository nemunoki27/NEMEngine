#include "SceneAuthoring.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <cctype>
#include <unordered_set>

//============================================================================
//	SceneAuthoring classMethods
//============================================================================
Engine::Entity Engine::SceneAuthoring::CreateGameObject(ECSWorld& world, const std::string_view& name) {

	return CreateGameObject(world, name, {});
}

Engine::Entity Engine::SceneAuthoring::CreateGameObject(ECSWorld& world,
	const std::string_view& name, std::span<const uint32_t> additionalTypeIDs) {

	ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
	std::vector<uint32_t> typeIDs;
	typeIDs.reserve(4 + additionalTypeIDs.size());
	typeIDs.emplace_back(registry.GetID<NameComponent>());
	typeIDs.emplace_back(registry.GetID<TransformComponent>());
	typeIDs.emplace_back(registry.GetID<HierarchyComponent>());
	typeIDs.emplace_back(registry.GetID<SceneObjectComponent>());
	typeIDs.insert(typeIDs.end(), additionalTypeIDs.begin(), additionalTypeIDs.end());

	// 最終アーキタイプへ一度だけ配置する
	Entity entity = world.CreateEntityWithComponents(typeIDs);
	world.GetComponent<NameComponent>(entity).name = std::string(name);
	world.GetComponent<SceneObjectComponent>(entity).localFileID = UUID::New();
	return entity;
}

void Engine::SceneAuthoring::EnsureGameObjectDefaults(ECSWorld& world,
	const Entity& entity, const std::string_view& defaultName) {

	// エンティティが存在しない場合は何もしない
	if (!world.IsAlive(entity)) {
		return;
	}

	// デフォルトコンポーネントの追加
	if (!world.HasComponent<NameComponent>(entity)) {

		auto& name = world.AddComponent<NameComponent>(entity);
		name.name = std::string(defaultName);
	}
	if (!world.HasComponent<TransformComponent>(entity)) {

		world.AddComponent<TransformComponent>(entity);
	}
	if (!world.HasComponent<HierarchyComponent>(entity)) {

		world.AddComponent<HierarchyComponent>(entity);
	}
	if (!world.HasComponent<SceneObjectComponent>(entity)) {

		auto& sceneObject = world.AddComponent<SceneObjectComponent>(entity);
		sceneObject.localFileID = UUID::New();
	}

	auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
	if (!sceneObject.localFileID) {

		sceneObject.localFileID = UUID::New();
	}
}

bool Engine::SceneAuthoring::TryParseIndexedName(const std::string& name, std::string& outBase, uint32_t& outIndex) {

	// 末尾の "_数字" を切り出す、区切りが無い/数字でない場合は失敗
	const size_t pos = name.rfind('_');
	if (pos == std::string::npos || pos == 0 || pos + 1 >= name.size()) {
		return false;
	}
	for (size_t i = pos + 1; i < name.size(); ++i) {
		if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
			return false;
		}
	}
	outBase = name.substr(0, pos);
	outIndex = static_cast<uint32_t>(std::stoul(name.substr(pos + 1)));
	return true;
}

std::string Engine::SceneAuthoring::MakeUniqueEntityName(ECSWorld& world,
	const std::string_view& desiredName, const Entity& exclude) {

	const std::string base = desiredName.empty() ? "Entity" : std::string(desiredName);

	// ベース名そのものの使用有無と、base_N の N を集める
	bool baseUsed = false;
	std::unordered_set<uint32_t> usedIndices;
	world.ForEachAliveEntity([&](Entity entity) {

		if (entity == exclude || !world.HasComponent<NameComponent>(entity)) {
			return;
		}
		const std::string& current = world.GetComponent<NameComponent>(entity).name;
		if (current == base) {
			baseUsed = true;
			return;
		}
		std::string currentBase;
		uint32_t currentIndex = 0;
		if (TryParseIndexedName(current, currentBase, currentIndex) && currentBase == base) {
			usedIndices.insert(currentIndex);
		}
		});

	// 衝突が無ければ希望名のまま使う
	if (!baseUsed) {
		return base;
	}
	// 1から始まる最小の未使用インデックスを付ける
	uint32_t candidate = 1;
	while (usedIndices.contains(candidate)) {
		++candidate;
	}
	return base + "_" + std::to_string(candidate);
}
