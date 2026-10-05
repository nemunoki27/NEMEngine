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

// c++
#include <array>

namespace {

	struct PendingBufferElement {

		static constexpr bool kSerializable = false;
		static constexpr Engine::ComponentStorageKind kStorageKind = Engine::ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 2;
		int32_t value = 0;
	};

}

namespace NEMTests {

	void RegisterECSBindingTestComponents() {

		auto& registry = Engine::ComponentTypeRegistry::GetInstance();
		registry.Register<PendingBufferElement>(registry.GetComponentTypeCount(), "PendingBufferElement");
	}

	// 追加前の可変長配列も外部領域ごと安全に引き継ぐ
	bool CheckPendingBuffer() {

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
