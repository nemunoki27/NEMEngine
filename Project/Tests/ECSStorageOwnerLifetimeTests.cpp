#include "ECSStorageOwnerLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>

// c++
#include <array>
#include <memory>
#include <stdexcept>

namespace {

	// Componentの構築と破棄からWorldを終了する
	struct LifetimeProbe {

		static constexpr bool kHasECSHooks = true;
		static inline std::unique_ptr<Engine::ECSWorld>* owner = nullptr;
		static inline uint32_t phase = 0;
		static inline int liveCount = 0;
		static inline bool failAfterEnd = false;
		int value = 7;

		LifetimeProbe();
		LifetimeProbe(const LifetimeProbe& source);
		LifetimeProbe(LifetimeProbe&& source) noexcept;
		~LifetimeProbe();
		// 外部データの初期化は行わない
		static void InitializeStorage([[maybe_unused]] Engine::ECSWorld& world,
			[[maybe_unused]] const Engine::Entity& entity, [[maybe_unused]] LifetimeProbe& value);
		// 外部データの解放は行わない
		static void ReleaseStorage([[maybe_unused]] Engine::ECSWorld& world,
			[[maybe_unused]] const Engine::Entity& entity, [[maybe_unused]] LifetimeProbe& value);
		// 追加通知中の値を終了後も借用する
		static void OnAdded([[maybe_unused]] Engine::ECSWorld& world,
			[[maybe_unused]] const Engine::Entity& entity, LifetimeProbe& value);
		// 保存する値だけをJSONへ渡す
		static void SerializeECS([[maybe_unused]] const Engine::ECSWorld& world,
			[[maybe_unused]] const Engine::Entity& entity, const LifetimeProbe& value, nlohmann::json& out);
		// 読込処理中の値を終了後も借用する
		static void DeserializeECS([[maybe_unused]] Engine::ECSWorld& world,
			[[maybe_unused]] const Engine::Entity& entity, const nlohmann::json& in, LifetimeProbe& value);
	};

	LifetimeProbe::LifetimeProbe() {

		if (phase == 1) {
			owner->reset();
			if (failAfterEnd) {
				throw std::runtime_error("World終了後の構築失敗");
			}
		}
		value = 19;
		++liveCount;
	}

	LifetimeProbe::LifetimeProbe(const LifetimeProbe& source) : value(source.value) {

		if (phase == 2) {
			owner->reset();
			if (failAfterEnd) {
				throw std::runtime_error("World終了後の複製失敗");
			}
		}
		if (phase == 10) {
			(*owner)->GetCommandBuffer().Clear();
		}
		value = source.value + 3;
		++liveCount;
	}

	LifetimeProbe::LifetimeProbe(LifetimeProbe&& source) noexcept : value(source.value) {

		if (phase == 3) {
			owner->reset();
		}
		source.value = 0;
		++liveCount;
	}

	LifetimeProbe::~LifetimeProbe() {

		if (phase == 4 && owner && *owner) {
			owner->reset();
		}
		--liveCount;
	}

	void LifetimeProbe::InitializeStorage([[maybe_unused]] Engine::ECSWorld& world,
		[[maybe_unused]] const Engine::Entity& entity, [[maybe_unused]] LifetimeProbe& value) {
}

	void LifetimeProbe::ReleaseStorage([[maybe_unused]] Engine::ECSWorld& world,
		[[maybe_unused]] const Engine::Entity& entity, [[maybe_unused]] LifetimeProbe& value) {
}

	void LifetimeProbe::OnAdded([[maybe_unused]] Engine::ECSWorld& world,
		[[maybe_unused]] const Engine::Entity& entity, LifetimeProbe& value) {

		if (phase == 6) {
			owner->reset();
			value.value = 43;
		}
	}

	void LifetimeProbe::SerializeECS([[maybe_unused]] const Engine::ECSWorld& world,
		[[maybe_unused]] const Engine::Entity& entity, const LifetimeProbe& value, nlohmann::json& out) {

		out = value.value;
	}

	void LifetimeProbe::DeserializeECS([[maybe_unused]] Engine::ECSWorld& world,
		[[maybe_unused]] const Engine::Entity& entity, const nlohmann::json& in, LifetimeProbe& value) {

		if (LifetimeProbe::phase == 8) {
			LifetimeProbe::owner->reset();
		}
		value.value = in.get<int>();
	}

	// Storageの構築途中で所有Worldを終了する
	struct EndingStorage {

		static inline std::unique_ptr<Engine::ECSWorld>* owner = nullptr;
		static inline int liveCount = 0;
		int value = 0;

		EndingStorage();
		~EndingStorage();
	};

	EndingStorage::EndingStorage() {

		owner->reset();
		value = 31;
		++liveCount;
	}

	EndingStorage::~EndingStorage() {

		--liveCount;
	}

	// 終了したWorldの構造変更を再開しない
	bool CheckComponentEnd(uint32_t mode, bool fail = false) {

		using namespace Engine;
		auto owned = std::make_unique<ECSWorld>();
		LifetimeProbe::owner = &owned;
		LifetimeProbe::phase = 0;
		LifetimeProbe::failAfterEnd = fail;
		const auto lifetime = owned->GetLifetime();
		const Entity entity = owned->CreateEntity();
		const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(
			ComponentTypeRegistry::GetInstance().GetID<LifetimeProbe>());
		std::unique_ptr<PendingComponent> pending;
		if (mode == 2) {
			pending = std::make_unique<PendingComponent>(info, 1000);
		} else if (mode == 9) {
			owned->GetCommandBuffer().StageAddComponent(*owned, entity, info.id);
		} else if (mode == 3 || mode == 4 || mode == 5 || mode == 8) {
			owned->AddComponent<LifetimeProbe>(entity);
		}
		LifetimeProbe::phase = mode == 0 ? 1 : mode == 7 ? 6 : mode == 9 ? 2 : mode;
		bool rejected = false;
		try {
			if (mode == 0 || mode == 7) {
				const std::array types{info.id};
				owned->CreateEntityWithComponents(types);
			} else if (mode == 1 || mode == 6) {
				owned->AddComponent<LifetimeProbe>(entity);
			} else if (mode == 2) {
				owned->ApplyPendingComponent(entity, *pending);
			} else if (mode == 3) {
				owned->AddComponent<NameComponent>(entity);
			} else if (mode == 4) {
				owned->DestroyEntity(entity);
				owned->FlushPendingDestroyEntities();
			} else if (mode == 8) {
				owned->ApplyComponentJson(entity, "LifetimeProbe", 47);
			} else if (mode == 9) {
				owned->AddComponent<LifetimeProbe>(entity);
			} else {
				owned->ForEach<LifetimeProbe>([&](const Entity&, LifetimeProbe& value) {
					owned.reset();
					value.value = 41;
					if (value.value != 41 || LifetimeProbe::liveCount != 1) {
						throw std::logic_error("走査中のComponentが解放されました");
					}
				});
			}
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		LifetimeProbe::phase = 0;
		LifetimeProbe::failAfterEnd = false;
		pending.reset();
		LifetimeProbe::owner = nullptr;
		return rejected && !owned && !lifetime->IsAlive() && LifetimeProbe::liveCount == 0;
	}

	// Registryの直接取得でも構築終了まで所有先を保持する
	bool CheckStorageEnd() {

		auto owned = std::make_unique<Engine::ECSWorld>();
		EndingStorage::owner = &owned;
		const auto lifetime = owned->GetLifetime();
		bool rejected = false;
		try {
			owned->GetStorage().Get<EndingStorage>();
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		EndingStorage::owner = nullptr;
		return rejected && !owned && !lifetime->IsAlive() && EndingStorage::liveCount == 0;
	}

	// 終了状態の参照だけではComponentを保持しない
	bool CheckLifetimeOnly() {

		auto owned = std::make_unique<Engine::ECSWorld>();
		owned->AddComponent<LifetimeProbe>(owned->CreateEntity());
		const auto lifetime = owned->GetLifetime();
		owned.reset();
		return !lifetime->IsAlive() && LifetimeProbe::liveCount == 0;
	}

	// 借用中の予約は失効しても値を残す
	bool CheckPendingLease() {

		using namespace Engine;
		const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(
			ComponentTypeRegistry::GetInstance().GetID<LifetimeProbe>());
		auto pending = std::make_shared<PendingComponent>(info, 2000);
		auto lease = pending->AcquireValueLease();
		auto* value = static_cast<LifetimeProbe*>(pending->GetData());
		pending->Cancel();
		if (pending->GetInstanceID() != 0 || pending->GetData() || LifetimeProbe::liveCount != 1) {
			return false;
		}
		pending.reset();
		value->value = 53;
		if (value->value != 53 || LifetimeProbe::liveCount != 1) {
			return false;
		}
		lease.reset();
		return LifetimeProbe::liveCount == 0;
	}

	// コピー途中の予約取消で古い値を公開しない
	bool CheckCopyCancellation() {

		using namespace Engine;
		auto owned = std::make_unique<ECSWorld>();
		LifetimeProbe::owner = &owned;
		const auto entity = owned->CreateEntity();
		const auto typeID = ComponentTypeRegistry::GetInstance().GetID<LifetimeProbe>();
		owned->GetCommandBuffer().StageAddComponent(*owned, entity, typeID);
		LifetimeProbe::phase = 10;
		bool rejected = false;
		try {
			owned->AddComponent<LifetimeProbe>(entity);
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		LifetimeProbe::phase = 0;
		LifetimeProbe::owner = nullptr;
		return rejected && owned->IsAlive(entity) && !owned->HasComponent<LifetimeProbe>(entity) &&
			LifetimeProbe::liveCount == 0 && owned->GetBindingComponentInstanceID(entity, typeID) == 0;
	}
}

bool NEMTests::CheckECSStorageOwnerLifetime() {

	auto& registry = Engine::ComponentTypeRegistry::GetInstance();
	registry.Register<LifetimeProbe>(registry.GetComponentTypeCount(), "LifetimeProbe");
	for (uint32_t mode = 0; mode < 10; ++mode) {
		if (!CheckComponentEnd(mode)) {
			return false;
		}
	}
	return CheckComponentEnd(0, true) && CheckComponentEnd(2, true) &&
		CheckStorageEnd() && CheckLifetimeOnly() && CheckPendingLease() && CheckCopyCancellation();
}
