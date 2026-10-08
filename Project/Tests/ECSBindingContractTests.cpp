#include "ECSBindingContractTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

// c++
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

	struct PendingBufferElement {

		static constexpr bool kSerializable = false;
		static constexpr Engine::ComponentStorageKind kStorageKind = Engine::ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 2;
		int32_t value = 0;
	};

	// 追加予約の構築中に所有元を変更する
	struct PendingConstructionProbe {

		static constexpr bool kSerializable = false;
		static inline std::function<void()> action;
		static inline uint32_t destroyed = 0;

		PendingConstructionProbe() {

			if (action) {
				action();
			}
		}
		PendingConstructionProbe(const PendingConstructionProbe&) = default;
		PendingConstructionProbe(PendingConstructionProbe&&) noexcept = default;
		~PendingConstructionProbe() noexcept { ++destroyed; }
	};

	// 通知で元の文字列が変わっても入力名を保持する
	bool CheckBorrowedNames() {

		using namespace Engine;
		for (uint32_t mode = 0; mode < 3; ++mode) {
			ECSWorld world;
			const auto typeID = ComponentTypeRegistry::GetInstance().GetID<NameComponent>();
			std::string requested =
				mode == 2 ? ComponentTypeRegistry::GetInstance().GetInfo(typeID).name : std::string(512, 'A');
			const std::string expected = mode == 2 ? "Loaded" : requested;
			const auto initial = mode == 0 ? Entity::Null() : world.CreateEntity();
			struct NameMutation {

				std::string& input;
				uint32_t typeID;
				ComponentMutationKind kind;
				uint32_t calls = 0;
			} state{requested, typeID, mode == 0 ? ComponentMutationKind::EntityCreated : ComponentMutationKind::Added};
			const auto listener = world.AddComponentMutationListener(
				[](ECSWorld&, const Entity&, uint32_t typeID, ComponentMutationKind kind, void* data) {
					auto& state = *static_cast<NameMutation*>(data);
					if (kind == state.kind && (kind == ComponentMutationKind::EntityCreated || typeID == state.typeID)) {
						state.input.assign(4096, 'B');
						++state.calls;
					}
				},
				&state);
			const ScopedCleanup cleanup([&]() noexcept { world.RemoveComponentMutationListener(listener); });
			Entity result = initial;
			if (mode == 0) {
				result = SceneAuthoring::CreateGameObject(world, requested);
			} else if (mode == 1) {
				SceneAuthoring::EnsureGameObjectDefaults(world, result, requested);
			} else if (!world.AddComponentFromJson(result, requested, nlohmann::json{{"name", expected}})) {
				return false;
			}
			if (state.calls != 1 || world.GetComponent<NameComponent>(result).name != expected) {
				return false;
			}
		}
		return true;
	}

	// 構築後に失効したWorldやEntityへ予約を公開しない
	bool CheckPendingConstruction() {

		using namespace Engine;
		const auto typeID = ComponentTypeRegistry::GetInstance().GetID<PendingConstructionProbe>();
		for (uint32_t mode = 0; mode < 3; ++mode) {
			auto owned = std::make_unique<ECSWorld>();
			const auto lifetime = owned->GetLifetime();
			const auto entity = owned->CreateEntity();
			PendingConstructionProbe::destroyed = 0;
			PendingConstructionProbe::action = [&]() {
				if (mode == 2) {
					owned->DestroyEntity(entity);
				} else {
					owned.reset();
					if (mode == 1) {
						throw std::runtime_error("Component構築失敗の検証");
					}
				}
			};
			const ScopedCleanup cleanup([]() noexcept { PendingConstructionProbe::action = {}; });
			bool rejected = false;
			try {
				const auto instanceID = owned->GetCommandBuffer().StageAddComponent(*owned, entity, typeID);
				rejected = mode == 2 && instanceID == 0 && !owned->GetCommandBuffer().FindPendingComponent(entity, typeID);
			} catch (const std::runtime_error& error) {
				rejected = mode == 0 || (mode == 1 && std::string(error.what()) == "Component構築失敗の検証");
			}
			if (!rejected || lifetime->IsAlive() != (mode == 2) ||
				PendingConstructionProbe::destroyed != (mode == 1 ? 0u : 1u)) {
				return false;
			}
		}
		return true;
	}

	// 追加通知で削除や再追加されたBufferを返さない
	bool CheckBufferAdditionNotification(uint32_t mode) {

		using namespace Engine;
		ECSWorld world;
		const Entity entity = world.CreateEntity();
		struct NotificationState {

			uint32_t typeID;
			uint32_t mode;
			uint32_t calls = 0;
			uint64_t instanceID = 0;
		};
		NotificationState state{ComponentTypeRegistry::GetInstance().GetID<PendingBufferElement>(), mode};
		world.AddComponentMutationListener(
			[](ECSWorld& current, const Entity& target, uint32_t typeID, ComponentMutationKind kind, void* data) {
				auto& state = *static_cast<NotificationState*>(data);
				if (typeID != state.typeID || kind != ComponentMutationKind::Added || state.calls != 0) {
					return;
				}
				++state.calls;
				state.instanceID = current.GetComponentInstanceID(target, typeID);
				if (state.mode < 2) {
					current.RemoveBuffer<PendingBufferElement>(target);
					if (state.mode == 1) {
						current.AddBuffer<PendingBufferElement>(target).EmplaceBack(PendingBufferElement{111});
					}
				} else {
					current.AddComponent<NameComponent>(target).name = "Moved";
				}
			}, &state);
		bool rejected = false;
		try {
			world.AddBuffer<PendingBufferElement>(entity).EmplaceBack(PendingBufferElement{222});
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		if (state.calls != 1 || rejected != (mode < 2)) {
			return false;
		}
		if (mode == 0) {
			return !world.HasBuffer<PendingBufferElement>(entity);
		}
		const auto buffer = std::as_const(world).GetBuffer<PendingBufferElement>(entity);
		return buffer.GetSize() == 1 && buffer[0].value == (mode == 1 ? 111 : 222) &&
			(world.GetComponentInstanceID(entity, state.typeID) == state.instanceID) == (mode == 2);
	}

	// Script削除の通知から格納先を変更する
	bool CheckScriptRemovalNotification(uint32_t mode, bool notifyScript) {

		using namespace Engine;
		ECSWorld world;
		const auto entity = world.CreateEntity();
		world.AddComponent<ScriptComponent>(entity);
		const auto removed = MakeScriptEntry("Removed", "Removed");
		world.GetBuffer<ScriptEntry>(entity).EmplaceBack(removed);
		struct NotificationState {

			uint32_t typeID;
			uint32_t mode;
			uint32_t calls = 0;
			ScriptEntry replacement = MakeScriptEntry("Replacement", "Replacement");
		};
		const auto& registry = ComponentTypeRegistry::GetInstance();
		NotificationState state{notifyScript ? registry.GetID<ScriptComponent>() : registry.GetID<ScriptEntry>(), mode};
		world.AddComponentMutationListener(
			[](ECSWorld& current, const Entity& target, uint32_t typeID, ComponentMutationKind kind, void* data) {
				auto& state = *static_cast<NotificationState*>(data);
				if (kind != ComponentMutationKind::Modified || typeID != state.typeID || state.calls != 0) {
					return;
				}
				++state.calls;
				if (state.mode == 0 || state.mode == 1) {
					current.RemoveBuffer<ScriptEntry>(target);
					if (state.mode == 1) {
						current.AddBuffer<ScriptEntry>(target).EmplaceBack(state.replacement);
					}
				} else if (state.mode == 2) {
					current.RemoveComponent<ScriptComponent>(target);
					current.AddComponent<ScriptComponent>(target);
					current.GetBuffer<ScriptEntry>(target).EmplaceBack(state.replacement);
				} else if (state.mode == 3) {
					current.AddComponent<NameComponent>(target).name = "Moved";
				} else {
					current.GetBuffer<ScriptEntry>(target).EmplaceBack(state.replacement);
				}
			},
			&state);
		world.GetCommandBuffer().EnqueueRemoveScript(entity, removed.scriptSlotID);
		world.FlushWorldCommands();
		if (state.calls != 1) {
			return false;
		}
		if (mode == 0) {
			return world.HasComponent<ScriptComponent>(entity) && !world.HasBuffer<ScriptEntry>(entity);
		}
		if (mode == 3) {
			return !world.HasComponent<ScriptComponent>(entity) && !world.HasBuffer<ScriptEntry>(entity) &&
				   world.GetComponent<NameComponent>(entity).name == "Moved";
		}
		const auto entries = std::as_const(world).GetBuffer<ScriptEntry>(entity);
		return world.HasComponent<ScriptComponent>(entity) && entries.GetSize() == 1 &&
			   entries[0].scriptSlotID == state.replacement.scriptSlotID;
	}

}

namespace NEMTests {

	void RegisterECSBindingTestComponents() {

		auto& registry = Engine::ComponentTypeRegistry::GetInstance();
		registry.Register<PendingBufferElement>(registry.GetComponentTypeCount(), "PendingBufferElement");
		registry.Register<PendingConstructionProbe>(registry.GetComponentTypeCount(), "PendingConstructionProbe");
	}

	// 追加前の可変長配列も外部領域ごと安全に引き継ぐ
	bool CheckPendingBuffer() {

		for (uint32_t mode = 0; mode < 3; ++mode) {
			if (!CheckBufferAdditionNotification(mode)) {
				return false;
			}
		}
		Engine::ECSWorld world;
		const auto entity = world.CreateEntity();
		const uint32_t typeID = Engine::ComponentTypeRegistry::GetInstance().GetID<PendingBufferElement>();
		const uint64_t instanceID = world.GetCommandBuffer().StageAddComponent(world, entity, typeID);
		const std::array<PendingBufferElement, 4> values{{{13}, {29}, {41}, {57}}};
		if (!world.TryGetBufferForBinding(entity, typeID).SetData(values.data(), static_cast<uint32_t>(values.size()))) {
			return false;
		}
		world.FlushWorldCommands();
		auto buffer = world.TryGetUntypedBuffer(entity, typeID);
		std::array<PendingBufferElement, 4> copied{};
		if (buffer.CopyTo(copied.data(), static_cast<uint32_t>(copied.size())) != values.size() || copied[0].value != 13 ||
			copied[3].value != 57 || world.GetComponentInstanceID(entity, typeID) != instanceID) {
			return false;
		}
		// 自分の部分列を詰め、範囲外の指定では内容を維持する
		auto* source = static_cast<PendingBufferElement*>(buffer.GetData());
		if (buffer.SetData(source + 3, 2) || !buffer.SetData(source + 1, 3)) {
			return false;
		}
		if (buffer.CopyTo(copied.data(), 4) != 3 || copied[0].value != 29 || copied[2].value != 57) {
			return false;
		}
		// 右へ重なるコピーでも元の順序を維持する
		source = static_cast<PendingBufferElement*>(buffer.GetData());
		return buffer.CopyTo(source + 1, 2) == 2 && source[1].value == 29 && source[2].value == 41;
	}

	bool CheckPendingGameObject() {

		if (!CheckBorrowedNames() || !CheckPendingConstruction()) {
			return false;
		}
		for (uint32_t mode = 0; mode < 5; ++mode) {
			for (bool notifyScript : {false, true}) {
				if (!CheckScriptRemovalNotification(mode, notifyScript)) {
					return false;
				}
			}
		}
		Engine::ECSWorld world;
		const auto scriptOwner = world.CreateEntity();
		world.AddComponent<Engine::ScriptComponent>(scriptOwner);
		auto scripts = world.GetBuffer<Engine::ScriptEntry>(scriptOwner);
		const auto first = Engine::MakeScriptEntry("Test", "Test");
		const auto second = Engine::MakeScriptEntry("Test", "Test");
		scripts.EmplaceBack(first);
		scripts.EmplaceBack(second);
		world.GetCommandBuffer().EnqueueRemoveScript(scriptOwner, first.scriptSlotID);
		if (scripts.GetSize() != 2) {
			return false;
		}
		world.GetCommandBuffer().Flush(world);
		// 同型の二つ目は残り、重複削除でも新しいslotへ触れない
		scripts = world.GetBuffer<Engine::ScriptEntry>(scriptOwner);
		if (scripts.GetSize() != 1 || scripts[0].scriptSlotID != second.scriptSlotID) {
			return false;
		}
		world.GetCommandBuffer().EnqueueRemoveScript(scriptOwner, first.scriptSlotID);
		world.GetCommandBuffer().EnqueueRemoveScript(scriptOwner, second.scriptSlotID);
		world.GetCommandBuffer().EnqueueRemoveScript(scriptOwner, second.scriptSlotID);
		world.GetCommandBuffer().Flush(world);
		if (world.HasComponent<Engine::ScriptComponent>(scriptOwner) || world.HasBuffer<Engine::ScriptEntry>(scriptOwner)) {
			return false;
		}
		const auto entity = world.CreateEntity();
		world.GetCommandBuffer().EnqueueCreateEntity(world, entity, "Pending", Engine::Entity::Null());
		const auto* pendingMembership = world.TryGetComponentForBinding<Engine::SceneObjectComponent>(entity);
		if (!pendingMembership || !pendingMembership->localFileID) {
			return false;
		}
		const auto reservedLocalID = pendingMembership->localFileID;
		auto* transform = world.TryGetComponentForBinding<Engine::TransformComponent>(entity);
		const auto typeID = Engine::ComponentTypeRegistry::GetInstance().GetID<Engine::TransformComponent>();
		const auto instanceID = world.GetBindingComponentInstanceID(entity, typeID);
		if (!transform || !instanceID || world.HasComponent<Engine::TransformComponent>(entity)) {
			return false;
		}

		// 生成直後の値を読み戻し、通常走査へはまだ公開しない
		transform->localPos = Engine::Vector3(13.0f, 17.0f, 19.0f);
		world.TryGetComponentForBinding<Engine::NameComponent>(entity)->name = "Edited";
		Engine::ResolvedWorldTransform resolved{};
		if (Engine::TransformWorldUtility::ResolveWorldTransform(world, entity, resolved) ||
			!Engine::TransformWorldUtility::ResolveWorldTransform(world, entity, resolved, true) ||
			resolved.matrix.GetTranslationValue().x != 13.0f) {
			return false;
		}

		// 実体化でも個体番号と編集値を引き継ぐ
		world.GetCommandBuffer().Flush(world);
		return world.GetComponentInstanceID(entity, typeID) == instanceID &&
			   world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID == reservedLocalID &&
			   world.GetComponent<Engine::TransformComponent>(entity).localPos.y == 17.0f &&
			   world.GetComponent<Engine::NameComponent>(entity).name == "Edited" &&
			   static_cast<bool>(world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID);
	}
}
