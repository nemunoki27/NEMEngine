#include "ECSQueryContractTests.h"
#include "ECSSerializationLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/ECSCreationScope.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// c++
#include <array>
#include <stdexcept>
#include <memory>
#include <type_traits>

namespace {

	// 有効状態と保存値を持つ検証用Component
	struct QueryComponent {

		static constexpr bool kEnableable = true; // 有効状態を独立して保持
		int value = 0;							  // 複製後に照合する値
	};

	void to_json(nlohmann::json& out, const QueryComponent& value) {

		out = value.value;
	}

	void from_json(const nlohmann::json& in, QueryComponent& value) {

		value.value = in.get<int>();
	}

	// Worldを終了させるhookの種類
	enum class EndingHook {
		None,
		Initialize,
		Added,
		Removed,
		Release
	};

	// 通知元のWorld終了を発生させるComponent
	struct WorldEndingComponent {

		static constexpr bool kSerializable = false;
		static constexpr bool kHasECSHooks = true;
		static inline std::unique_ptr<Engine::ECSWorld>* owner = nullptr;
		static inline EndingHook ending = EndingHook::None;

		// 指定されたhookだけで所有元を終了する
		static void EndWorld(EndingHook hook);
		static void InitializeStorage(Engine::ECSWorld&, const Engine::Entity&, WorldEndingComponent&) {
			EndWorld(EndingHook::Initialize);
		}
		static void OnAdded(Engine::ECSWorld&, const Engine::Entity&, WorldEndingComponent&) { EndWorld(EndingHook::Added); }
		static void OnRemoved(Engine::ECSWorld&, const Engine::Entity&) { EndWorld(EndingHook::Removed); }
		static void ReleaseStorage(Engine::ECSWorld&, const Engine::Entity&, WorldEndingComponent&) {
			EndWorld(EndingHook::Release);
		}
	};

	void WorldEndingComponent::EndWorld(EndingHook hook) {

		if (ending == hook) {
			ending = EndingHook::None;
			owner->reset();
		}
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
	registry.Register<WorldEndingComponent>(registry.GetComponentTypeCount(), "WorldEndingComponent");
	RegisterECSSerializationTestComponents();
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
	// 読取Queryも無効なComponentを除いて同じ列を走査する
	const Engine::ECSWorld& readOnly = world;
	static_assert(std::is_const_v<std::remove_reference_t<decltype(readOnly.GetComponent<QueryComponent>(entity))>>);
	if (readOnly.GetComponent<QueryComponent>(entity).value != 19) {
		return false;
	}
	uint32_t readCount = 0;
	readOnly.ForEach<QueryComponent>([&](Engine::Entity, const QueryComponent&) { ++readCount; });
	if (readCount != 0) {
		return false;
	}
	world.SetComponentEnabled<QueryComponent>(entity, true);
	bool migrationRejected = false;
	readOnly.ForEach<QueryComponent>([&](Engine::Entity current, auto& component) {
		static_assert(std::is_const_v<std::remove_reference_t<decltype(component)>>);
		readOnly.ForEach<QueryComponent>(
			[&](Engine::Entity, const QueryComponent& nested) { readCount += nested.value == 19 ? 1 : 0; });
		try {
			world.RemoveComponentByName(current, "QueryTag");
		} catch (const std::logic_error&) {
			migrationRejected = true;
		}
	});
	if (readCount != 1 || !migrationRejected || !world.HasComponent<QueryTag>(entity)) {
		return false;
	}
	// 走査中のWorld終了を残りの行へ持ち越さない
	for (uint32_t mode = 0; mode < 4; ++mode) {
		auto owned = std::make_unique<Engine::ECSWorld>();
		for (uint32_t index = 0; index < 2; ++index) {
			owned->AddComponent<QueryComponent>(owned->CreateEntity()).value = 19;
		}
		uint32_t called = 0;
		bool ended = false;
		try {
			if (mode == 0) {
				owned->ForEach<QueryComponent>([&](Engine::Entity, QueryComponent&) {
					++called;
					owned.reset();
				});
			} else if (mode == 1) {
				const Engine::ECSWorld& view = *owned;
				view.ForEach<QueryComponent>([&](Engine::Entity, const QueryComponent&) {
					++called;
					owned.reset();
				});
			} else if (mode == 2) {
				owned->ForEach(typeID, [&](Engine::Entity) {
					++called;
					owned.reset();
				});
			} else {
				owned->ForEachAliveEntity([&](Engine::Entity) {
					++called;
					owned.reset();
				});
			}
		} catch (const std::runtime_error&) {
			ended = true;
		}
		if (!ended || owned || called != 1) {
			return false;
		}
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

// 通知とhookでWorldが終了しても取消処理へ戻らない
bool NEMTests::CheckNotificationWorldLifetime() {

	using namespace Engine;
	// 各操作で終了させる通知を選ぶ
	constexpr std::array<ComponentMutationKind, 10> notificationKinds{ComponentMutationKind::EntityCreated,
		ComponentMutationKind::Added, ComponentMutationKind::Removed, ComponentMutationKind::Modified,
		ComponentMutationKind::EntityDestroyed, ComponentMutationKind::Modified, ComponentMutationKind::Added,
		ComponentMutationKind::EntityDestroyed, ComponentMutationKind::EntityCreated, ComponentMutationKind::Added};
	for (uint32_t mode = 0; mode < 10; ++mode) {
		for (bool failAfterEnding : {false, true}) {
			auto owned = std::make_unique<ECSWorld>();
			const Entity entity = owned->CreateEntity();
			owned->AddComponent<SceneObjectComponent>(entity);
			if (mode == 2 || mode == 3) {
				owned->AddComponent<QueryComponent>(entity);
			}
			std::unique_ptr<ECSCreationScope> scope;
			if (mode == 7 || mode == 8) {
				scope = std::make_unique<ECSCreationScope>(*owned);
				if (mode == 7) {
					owned->CreateEntity();
				}
			}
			const ComponentMutationKind kind = notificationKinds[mode];
			struct EndingNotification {
				std::unique_ptr<ECSWorld>& owner; // 終了するWorldの所有元
				ComponentMutationKind kind;		  // 終了させる通知
				bool fail;						  // 終了後の例外送出
				uint32_t called = 0;			  // 通知回数
			} state{owned, kind, failAfterEnding};
			owned->AddComponentMutationListener(
				[](ECSWorld&, const Entity&, uint32_t, ComponentMutationKind kind, void* data) {
					auto& state = *static_cast<EndingNotification*>(data);
					if (kind == state.kind) {
						++state.called;
						state.owner.reset();
						if (state.fail) {
							throw std::runtime_error("通知失敗の検証");
						}
					}
				},
				&state);
			uint32_t remainingCalls = 0;
			owned->AddComponentMutationListener(
				[](ECSWorld&, const Entity&, uint32_t, ComponentMutationKind, void* data) { ++*static_cast<uint32_t*>(data); },
				&remainingCalls);
			const auto lifetime = owned->GetLifetime();
			bool rejected = false;
			try {
				switch (mode) {
				case 0:
					owned->CreateEntity();
					break;
				case 1:
					owned->AddComponent<QueryComponent>(entity);
					break;
				case 2:
					owned->RemoveComponent<QueryComponent>(entity);
					break;
				case 3:
					owned->MarkComponentModified<QueryComponent>(entity);
					break;
				case 4:
					owned->DestroyEntity(entity);
					owned->FlushPendingDestroyEntities();
					break;
				case 5:
					SceneObjectUtility::SetActiveSelf(*owned, entity, false);
					break;
				case 6:
					owned->GetCommandBuffer().EnqueueAddComponentByName(entity, "QueryComponent");
					owned->FlushWorldCommands();
					break;
				case 7:
					scope->Rollback();
					break;
				case 8:
					owned->CreateEntity();
					break;
				case 9: {
					WorldCommandBuffer commands;
					commands.EnqueueAddComponentByName(entity, "QueryComponent");
					commands.Flush(*owned);
					break;
				}
				}
			} catch (const std::runtime_error& error) {
				rejected = !failAfterEnding || std::string(error.what()) == "通知失敗の検証";
			}
			if (!rejected || owned || lifetime->IsAlive() || state.called != 1 || remainingCalls != 0) {
				return false;
			}
			// 終了したWorldへのCommitとscope解放も安全に済ませる
			if (scope) {
				scope->Commit();
				if (scope->HasCreations()) {
					return false;
				}
			}
			WorldEndingComponent::owner = nullptr;
		}
	}
	constexpr std::array<EndingHook, 7> endingHooks{EndingHook::Initialize, EndingHook::Added, EndingHook::Initialize,
		EndingHook::Added, EndingHook::Release, EndingHook::Removed, EndingHook::Release};
	for (uint32_t mode = 0; mode < 7; ++mode) {
		auto owned = std::make_unique<ECSWorld>();
		const Entity entity = owned->CreateEntity();
		if (mode >= 4) {
			owned->AddComponent<WorldEndingComponent>(entity);
		}
		WorldEndingComponent::owner = &owned;
		WorldEndingComponent::ending = endingHooks[mode];
		const auto lifetime = owned->GetLifetime();
		bool rejected = false;
		try {
			if (mode < 2) {
				EntitySignature signature{};
				signature.Set(ComponentTypeRegistry::GetInstance().GetID<WorldEndingComponent>());
				owned->CreateEntityWithSignature(signature);
			} else if (mode < 4) {
				owned->AddComponent<WorldEndingComponent>(entity);
			} else if (mode < 6) {
				owned->RemoveComponent<WorldEndingComponent>(entity);
			} else {
				owned->DestroyEntity(entity);
				owned->FlushPendingDestroyEntities();
			}
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		WorldEndingComponent::ending = EndingHook::None;
		WorldEndingComponent::owner = nullptr;
		if (!rejected || owned || lifetime->IsAlive()) {
			return false;
		}
	}
	return true;
}
