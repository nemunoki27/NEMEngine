#include "ECSSerializationLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

// c++
#include <functional>
#include <memory>
#include <stdexcept>

namespace {

	// 保存hookから所有元や予約へ操作する検証用Component
	struct SerializationProbe {

		static inline std::function<void()> saveAction; // 保存中の操作
		static inline std::function<void()> loadAction; // 読込中の操作
		static inline std::function<void()> copyAction; // 複製中の操作
		int value = 7;									// 保存値

		SerializationProbe() = default;
		SerializationProbe(const SerializationProbe& source);
		SerializationProbe(SerializationProbe&& source) noexcept = default;
	};

	SerializationProbe::SerializationProbe(const SerializationProbe& source) : value(source.value) {

		if (copyAction) {
			copyAction();
		}
	}

	void SerializeComponent([[maybe_unused]] const Engine::ECSWorld& world, [[maybe_unused]] const Engine::Entity& entity,
		const SerializationProbe& value, nlohmann::json& out) {

		out = value.value;
		if (SerializationProbe::saveAction) {
			SerializationProbe::saveAction();
		}
	}

	void DeserializeComponent([[maybe_unused]] Engine::ECSWorld& world, [[maybe_unused]] const Engine::Entity& entity,
		const nlohmann::json& in, SerializationProbe& value) {

		value.value = in.get<int>();
		if (SerializationProbe::loadAction) {
			SerializationProbe::loadAction();
		}
	}

	// 予約取消後に参照しない別の保存値
	struct SerializationTail {

		int value = 19;
	};

	void to_json(nlohmann::json& out, const SerializationTail& value) {

		out = value.value;
	}

	void from_json(const nlohmann::json& in, SerializationTail& value) {

		value.value = in.get<int>();
	}

	// 保存と読込の途中終了を呼出元へ伝える
	bool CheckWorldEnd(uint32_t mode, bool fail) {

		using namespace Engine;
		auto owned = std::make_unique<ECSWorld>();
		const Entity entity = owned->CreateEntity();
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<SerializationProbe>();
		if (mode == 2 || mode == 3) {
			owned->GetCommandBuffer().StageAddComponent(*owned, entity, typeID);
		} else {
			owned->AddComponent<SerializationProbe>(entity);
		}
		const auto lifetime = owned->GetLifetime();
		uint32_t calls = 0;
		std::function<void()> end = [&] {
			++calls;
			owned.reset();
			if (fail) {
				throw std::runtime_error("serialization hook failure");
			}
		};
		const ScopedCleanup cleanup([]() noexcept {
			SerializationProbe::saveAction = {};
			SerializationProbe::loadAction = {};
		});
		SerializationProbe::saveAction = mode < 4 ? end : std::function<void()>{};
		SerializationProbe::loadAction = mode == 4 ? end : std::function<void()>{};
		bool rejected = false;
		try {
			nlohmann::json data;
			if (mode == 0 || mode == 2) {
				owned->SerializeEntityComponents(entity, data);
			} else if (mode == 1 || mode == 3) {
				owned->SerializeComponentToJson(entity, "SerializationProbe", data);
			} else {
				owned->ApplyComponentJson(entity, "SerializationProbe", 29);
			}
		} catch (const std::runtime_error& error) {
			rejected = !fail || std::string(error.what()) == "serialization hook failure";
		}
		return rejected && !owned && !lifetime->IsAlive() && calls == 1;
	}

	// 保存hookの構造移動を拒否し、終了後は通常操作へ戻る
	bool CheckSaveMigration() {

		using namespace Engine;
		ECSWorld world;
		const auto entity = world.CreateEntity();
		world.AddComponent<SerializationProbe>(entity);
		const ScopedCleanup cleanup([]() noexcept { SerializationProbe::saveAction = {}; });
		SerializationProbe::saveAction = [&] { world.AddComponent<NameComponent>(entity); };
		for (bool single : {false, true}) {
			bool rejected = false;
			try {
				nlohmann::json data;
				if (single) {
					world.SerializeComponentToJson(entity, "SerializationProbe", data);
				} else {
					world.SerializeEntityComponents(entity, data);
				}
			} catch (const std::logic_error&) {
				rejected = true;
			}
			if (!rejected || world.HasComponent<NameComponent>(entity)) {
				return false;
			}
		}
		world.AddComponent<NameComponent>(entity).name = "AfterSave";
		return world.GetComponent<NameComponent>(entity).name == "AfterSave";
	}

	// 予約取消で失効した値と後続予約を保存しない
	bool CheckPendingCancellation(bool single) {

		using namespace Engine;
		ECSWorld world;
		const auto entity = world.CreateEntity();
		auto& registry = ComponentTypeRegistry::GetInstance();
		world.GetCommandBuffer().StageAddComponent(world, entity, registry.GetID<SerializationProbe>());
		world.GetCommandBuffer().StageAddComponent(world, entity, registry.GetID<SerializationTail>());
		const ScopedCleanup cleanup([]() noexcept { SerializationProbe::saveAction = {}; });
		SerializationProbe::saveAction = [&] { world.GetCommandBuffer().Clear(); };
		nlohmann::json data;
		if (single) {
			return !world.SerializeComponentToJson(entity, "SerializationProbe", data);
		}
		world.SerializeEntityComponents(entity, data);
		return !data.contains("SerializationProbe") && !data.contains("SerializationTail");
	}

	// 読込hookで置換した個体へ古い変更通知を送らない
	bool CheckLoadReplacement() {

		using namespace Engine;
		ECSWorld world;
		const auto entity = world.CreateEntity();
		world.AddComponent<SerializationProbe>(entity);
		uint32_t modified = 0;
		world.AddComponentMutationListener(
			[](ECSWorld&, const Entity&, uint32_t, ComponentMutationKind kind, void* state) {
				if (kind == ComponentMutationKind::Modified) {
					++*static_cast<uint32_t*>(state);
				}
			},
			&modified);
		const ScopedCleanup cleanup([]() noexcept { SerializationProbe::loadAction = {}; });
		SerializationProbe::loadAction = [&] {
			world.RemoveComponent<SerializationProbe>(entity);
			world.AddComponent<SerializationProbe>(entity).value = 99;
		};
		return !world.ApplyComponentJson(entity, "SerializationProbe", 29) && modified == 0 &&
			   world.GetComponent<SerializationProbe>(entity).value == 99;
	}

	// コピー処理で終了した元Worldを再参照しない
	bool CheckCloneWorldEnd(bool pending, bool fail) {

		using namespace Engine;
		auto owned = std::make_unique<ECSWorld>();
		const auto entity = owned->CreateEntity();
		if (pending) {
			owned->GetCommandBuffer().StageAddComponent(
				*owned, entity, ComponentTypeRegistry::GetInstance().GetID<SerializationProbe>());
		} else {
			owned->AddComponent<SerializationProbe>(entity);
		}
		const auto lifetime = owned->GetLifetime();
		uint32_t calls = 0;
		const ScopedCleanup cleanup([]() noexcept { SerializationProbe::copyAction = {}; });
		SerializationProbe::copyAction = [&] {
			++calls;
			owned.reset();
			if (fail) {
				throw std::runtime_error("clone hook failure");
			}
		};
		bool rejected = false;
		try {
			const auto snapshot = owned->CloneForSerialization();
		} catch (const std::runtime_error& error) {
			rejected = !fail || std::string(error.what()) == "clone hook failure";
		}
		return rejected && !owned && !lifetime->IsAlive() && calls == 1;
	}

	// 複製途中で取り消した予約をSnapshotへ渡さない
	bool CheckClonePendingCancellation(bool clear) {

		using namespace Engine;
		ECSWorld world;
		const auto entity = world.CreateEntity();
		auto& commands = world.GetCommandBuffer();
		auto& registry = ComponentTypeRegistry::GetInstance();
		commands.StageAddComponent(world, entity, registry.GetID<SerializationProbe>());
		commands.StageAddComponent(world, entity, registry.GetID<SerializationTail>());
		uint32_t calls = 0;
		const ScopedCleanup cleanup([]() noexcept { SerializationProbe::copyAction = {}; });
		SerializationProbe::copyAction = [&] {
			++calls;
			if (clear) {
				commands.Clear();
			} else {
				commands.EnqueueRemoveComponentByName(entity, "SerializationTail");
			}
		};
		bool rejected = false;
		try {
			const auto snapshot = world.CloneForSerialization();
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		return rejected && calls == 1 && world.IsAlive(entity) && !world.HasComponent<SerializationProbe>(entity);
	}
}

void NEMTests::RegisterECSSerializationTestComponents() {

	auto& registry = Engine::ComponentTypeRegistry::GetInstance();
	registry.Register<SerializationProbe>(registry.GetComponentTypeCount(), "SerializationProbe");
	registry.Register<SerializationTail>(registry.GetComponentTypeCount(), "SerializationTail");
}

bool NEMTests::CheckECSSerializationLifetime() {

	for (uint32_t mode = 0; mode < 5; ++mode) {
		for (bool fail : {false, true}) {
			if (!CheckWorldEnd(mode, fail)) {
				return false;
			}
		}
	}
	for (bool pending : {false, true}) {
		for (bool fail : {false, true}) {
			if (!CheckCloneWorldEnd(pending, fail)) {
				return false;
			}
		}
	}
	return CheckSaveMigration() && CheckPendingCancellation(false) && CheckPendingCancellation(true) &&
		   CheckLoadReplacement() && CheckClonePendingCancellation(false) && CheckClonePendingCancellation(true);
}
