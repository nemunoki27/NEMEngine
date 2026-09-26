#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/Scripting/Managed/ManagedBehavior.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <cstring>
#include <limits>

namespace {

	Engine::UntypedDynamicBuffer ResolveDynamicBuffer(
		Engine::ECSWorld* world, const Engine::Entity& entity,
		int32_t typeID, int32_t elementSize) {

		if (!world || !world->IsAlive(entity) ||
			typeID < 0 || elementSize <= 0) {
			return {};
		}
		Engine::ComponentTypeRegistry& registry =
			Engine::ComponentTypeRegistry::GetInstance();
		const uint32_t resolvedTypeID =
			static_cast<uint32_t>(typeID);
		if (resolvedTypeID >= registry.GetComponentTypeCount()) {
			return {};
		}
		const Engine::ComponentTypeInfo& info =
			registry.GetInfo(resolvedTypeID);
		if (info.storageKind != Engine::ComponentStorageKind::Buffer ||
			info.elementSize != static_cast<size_t>(elementSize) ||
			!info.bufferElementTriviallyCopyable) {
			return {};
		}
		return world->TryGetBufferForBinding(entity, resolvedTypeID);
	}
}

namespace Engine {

	//============================================================================
	//	オブジェクトモデルのコールバック
	//	C#側Entity/IComponentRef/ScriptBehaviour.Enabledから呼ばれる汎用操作
	//============================================================================

	int32_t ManagedScriptRuntime::HasComponentCallback(ManagedNativeEntity entity, int32_t typeID) {

		return GetComponentInstanceIDCallback(entity, typeID) != 0 ? 1 : 0;
	}

	uint64_t ManagedScriptRuntime::GetComponentInstanceIDCallback(ManagedNativeEntity entity, int32_t typeID) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeID < 0) {
			return 0;
		}
		const uint32_t componentTypeID = static_cast<uint32_t>(typeID);
		if (componentTypeID >= ComponentTypeRegistry::GetInstance().GetComponentTypeCount()) {
			return 0;
		}
		return world->GetBindingComponentInstanceID(resolved, componentTypeID);
	}

	void ManagedScriptRuntime::AddComponentCallback(ManagedNativeEntity entity, int32_t typeID) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeID < 0) {
			return;
		}
		auto& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeID) >= registry.GetComponentTypeCount()) {
			return;
		}
		// 走査中は値を保持し、安全地点で構造へ反映する
		try {
			world->GetCommandBuffer().StageAddComponent(*world, resolved, static_cast<uint32_t>(typeID));
		} catch (const std::exception& exception) {
			Logger::Output(LogType::Engine, spdlog::level::err, "Componentの追加予約に失敗しました: {}", exception.what());
		} catch (...) {
			Logger::Output(LogType::Engine, spdlog::level::err, "Componentの追加予約に失敗しました");
		}
	}

	void ManagedScriptRuntime::RemoveComponentCallback(ManagedNativeEntity entity, int32_t typeID) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeID < 0) {
			return;
		}
		auto& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeID) >= registry.GetComponentTypeCount()) {
			return;
		}
		// 対象無しの削除や適用前のentity失効もApply側で安全に無処理になる
		world->GetCommandBuffer().EnqueueRemoveComponentByName(resolved, registry.GetInfo(static_cast<uint32_t>(typeID)).name);
	}

	int32_t ManagedScriptRuntime::DynamicBufferLengthCallback(
		ManagedNativeEntity entity, int32_t typeID, int32_t elementSize) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const UntypedDynamicBuffer buffer =
			ResolveDynamicBuffer(world, resolved, typeID, elementSize);
		return buffer.IsValid() ?
			static_cast<int32_t>(buffer.GetSize()) : -1;
	}

	int32_t ManagedScriptRuntime::DynamicBufferCopyCallback(
		ManagedNativeEntity entity, int32_t typeID, int32_t elementSize,
		int32_t startIndex, void* destination, int32_t capacity) {

		if (startIndex < 0 || capacity < 0 ||
			(capacity != 0 && !destination)) {
			return -1;
		}
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const UntypedDynamicBuffer buffer =
			ResolveDynamicBuffer(world, resolved, typeID, elementSize);
		if (!buffer.IsValid()) {
			return -1;
		}
		return static_cast<int32_t>(buffer.CopyTo(
			destination, static_cast<uint32_t>(capacity),
			static_cast<uint32_t>(startIndex)));
	}

	int32_t ManagedScriptRuntime::DynamicBufferMutateCallback(
		ManagedNativeEntity entity, int32_t typeID, int32_t elementSize,
		int32_t operation, int32_t index, const void* data, int32_t count) {

		if (index < 0 || count < 0) {
			return 0;
		}
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		UntypedDynamicBuffer buffer =
			ResolveDynamicBuffer(world, resolved, typeID, elementSize);
		if (!buffer.IsValid()) {
			return 0;
		}

		bool success = false;
		switch (static_cast<ManagedDynamicBufferOperation>(operation)) {
		case ManagedDynamicBufferOperation::Replace:
			success = buffer.SetData(
				data, static_cast<uint32_t>(count));
			break;
		case ManagedDynamicBufferOperation::Append: {
			if (count != 0 && !data) {
				break;
			}
			const uint32_t oldSize = buffer.GetSize();
			const uint32_t appendCount = static_cast<uint32_t>(count);
			if (appendCount >
				(std::numeric_limits<uint32_t>::max)() - oldSize ||
				!buffer.Resize(oldSize + appendCount)) {
				break;
			}
			if (appendCount != 0) {
				std::memcpy(
					static_cast<std::byte*>(buffer.GetData()) +
					buffer.GetElementSize() * oldSize,
					data, buffer.GetElementSize() * appendCount);
			}
			success = true;
			break;
		}
		case ManagedDynamicBufferOperation::SetElement:
			success = buffer.SetElement(
				static_cast<uint32_t>(index), data);
			break;
		case ManagedDynamicBufferOperation::RemoveAt:
			success = buffer.RemoveAt(static_cast<uint32_t>(index));
			break;
		case ManagedDynamicBufferOperation::Resize:
			success = buffer.Resize(static_cast<uint32_t>(count));
			break;
		case ManagedDynamicBufferOperation::Clear:
			success = buffer.Resize(0);
			break;
		default:
			break;
		}

		if (success) {
			// Bakerと描画抽出へ同じフレーム内のBuffer変更を伝える
			world->MarkComponentModified(
				resolved, static_cast<uint32_t>(typeID));
		}
		return success ? 1 : 0;
	}

	void ManagedScriptRuntime::DestroyEntityCallback(ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}
		// コールバック中の即時破棄は走査を壊すため安全地点まで遅延する、重複破棄はApply側で吸収する
		world->GetCommandBuffer().EnqueueDestroyEntity(resolved);
	}

	int32_t ManagedScriptRuntime::GetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotID) {

		// 所有EntityとscriptSlotIDで実行中entryを特定し有効状態を返す、-1は未解決
		const Entity resolved = ResolveEntity(owner);
		return BehaviorSystem::GetScriptEnabled(resolved, UUID{ scriptSlotID });
	}

	void ManagedScriptRuntime::SetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotID, int32_t enabled) {

		const Entity resolved = ResolveEntity(owner);
		BehaviorSystem::SetScriptEnabled(resolved, UUID{ scriptSlotID }, enabled != 0);
	}

	ManagedScriptInstanceHandle ManagedScriptRuntime::GetScriptInstanceCallback(ManagedNativeEntity owner, const char* scriptTypeID) {

		// owner Entity上でscriptTypeID一致のscript instanceを引き、ManagedBehaviorならC#側handleを返す
		if (!scriptTypeID) {
			return ManagedScriptInstanceHandle::Null();
		}
		const Entity resolved = ResolveEntity(owner);
		MonoBehavior* instance = BehaviorSystem::FindScriptInstance(resolved, scriptTypeID);
		// C#由来のscriptだけがhandleを持つ、C++ MonoBehaviorはNullになる
		ManagedBehavior* managed = dynamic_cast<ManagedBehavior*>(instance);
		return managed ? managed->GetManagedHandle() : ManagedScriptInstanceHandle::Null();
	}

	int32_t ManagedScriptRuntime::AttachScriptCallback(ManagedNativeEntity owner, const char* scriptTypeID) {

		// owner EntityへscriptTypeIDのscriptをruntime attachし、instance生成の成否を返す
		if (!scriptTypeID) {
			return 0;
		}
		const Entity resolved = ResolveEntity(owner);
		return BehaviorSystem::AttachScript(resolved, scriptTypeID) ? 1 : 0;
	}

} // Engine
