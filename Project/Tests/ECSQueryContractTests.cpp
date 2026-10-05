#include "ECSQueryContractTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <stdexcept>

namespace {

	// 有効状態と保存値を持つ検証用Component
	struct QueryComponent {

		static constexpr bool kEnableable = true; // 有効状態を独立して保持
		int value = 0; // 複製後に照合する値
	};

	void to_json(nlohmann::json& out, const QueryComponent& value) {

		out = value.value;
	}

	void from_json(const nlohmann::json& in, QueryComponent& value) {

		value.value = in.get<int>();
	}

	// 値を持たない検証用Tag
	struct QueryTag {

		static constexpr Engine::ComponentStorageKind kStorageKind = Engine::ComponentStorageKind::Tag;
	};

}

void NEMTests::RegisterECSQueryTestComponents() {

	auto& registry = Engine::ComponentTypeRegistry::GetInstance();
	registry.Register<QueryComponent>(registry.GetComponentTypeCount(), "QueryComponent");
	registry.Register<QueryTag>(registry.GetComponentTypeCount(), "QueryTag");
}

// 型番号の走査条件と値を持たないTagの移動を確認する
bool NEMTests::CheckQueryModes() {

	Engine::ECSWorld world;
	const auto entity = world.CreateEntity();
	world.AddComponent<QueryComponent>(entity).value = 19;
	world.AddComponentByName(entity, "QueryTag");
	world.SetComponentEnabled<QueryComponent>(entity, false);
	const uint32_t typeID = Engine::ComponentTypeRegistry::GetInstance().GetID<QueryComponent>();
	uint32_t enabledCount = 0;
	uint32_t allCount = 0;
	world.ForEach(typeID, [&](const Engine::Entity&) { ++enabledCount; });
	world.ForEach(typeID, [&](const Engine::Entity&) { ++allCount; }, Engine::ECSQueryMode::IncludeDisabled);
	const auto snapshot = world.CloneForSerialization();
	if (enabledCount != 0 || allCount != 1 || !snapshot->HasComponent<QueryTag>(entity) ||
		snapshot->IsComponentEnabled<QueryComponent>(entity) || snapshot->GetComponent<QueryComponent>(entity).value != 19) {
		return false;
	}
	world.RemoveComponentByName(entity, "QueryTag");
	Engine::EntitySignature invalid{};
	invalid.Set(Engine::kMaxComponentTypes - 1);
	bool rejected = false;
	try {
		world.CreateEntityWithSignature(invalid);
	} catch (const std::invalid_argument&) {
		rejected = true;
	}
	return rejected && world.GetComponent<QueryComponent>(entity).value == 19;
}
