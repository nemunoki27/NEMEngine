#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

namespace Engine {

	//============================================================================
	//	Hierarchy Callbacks
	//	C#側のTransform (親子関係)クラスから呼び出されるネイティブ実装
	//============================================================================

	ManagedNativeEntity ManagedScriptRuntime::GetParentCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return MakeNullNativeEntity();
		}

		HierarchyComponent* hierarchy = world->TryGetComponent<HierarchyComponent>(resolved);
		if (!hierarchy) {
			return MakeNullNativeEntity();
		}

		// 親エンティティが存在すればラップして返し、なければNullEntity
		const Entity parent = hierarchy->parent;
		return world->IsAlive(parent) ? MakeNativeEntity(*world, parent) : MakeNullNativeEntity();
	}

	ManagedNativeEntity ManagedScriptRuntime::GetFirstChildCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return MakeNullNativeEntity();
		}

		HierarchyComponent* hierarchy = world->TryGetComponent<HierarchyComponent>(resolved);
		if (!hierarchy) {
			return MakeNullNativeEntity();
		}

		// 最初の子エンティティを返す、イテレーションの開始点に使用
		const Entity child = hierarchy->firstChild;
		return world->IsAlive(child) ? MakeNativeEntity(*world, child) : MakeNullNativeEntity();
	}

	ManagedNativeEntity ManagedScriptRuntime::GetNextSiblingCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return MakeNullNativeEntity();
		}

		HierarchyComponent* hierarchy = world->TryGetComponent<HierarchyComponent>(resolved);
		if (!hierarchy) {
			return MakeNullNativeEntity();
		}

		// 同じ親を持つ次の兄弟エンティティを返す
		const Entity sibling = hierarchy->nextSibling;
		return world->IsAlive(sibling) ? MakeNativeEntity(*world, sibling) : MakeNullNativeEntity();
	}

	void ManagedScriptRuntime::SetParentCallback(ManagedNativeEntity entity, ManagedNativeEntity parent) {

		EnqueueSetParentCommand(entity, parent, false);
	}

	void ManagedScriptRuntime::EnqueueSetParentCommand(
		ManagedNativeEntity childEntity, ManagedNativeEntity parentEntity, bool worldPositionStays) {

		ECSWorld* world = ResolveWorld(childEntity);
		const Entity child = ResolveEntity(childEntity);
		// 子が無効なら何もしない
		if (!world || !world->IsAlive(child)) {
			return;
		}

		Entity newParent = Entity::Null();
		const bool hasParent = parentEntity.world.index != 0xFFFFFFFFu &&
			parentEntity.world.generation != 0 && parentEntity.index != 0xFFFFFFFFu;
		if (hasParent) {

			ECSWorld* parentWorld = ResolveWorld(parentEntity);
			if (parentWorld != world) {

				Logger::Output(LogType::Engine, spdlog::level::warn,
					"ManagedScript: 別WorldのEntityは親に設定できません");
				return;
			}
			const Entity candidate = ResolveEntity(parentEntity);
			if (world->IsAlive(candidate)) {
				newParent = candidate;
			}
		}
		// 親子付けはHierarchyComponentの追加やリンク繋ぎ替えを伴う構造変更のためForEach走査を壊さないようコマンドバッファへ積み安全地点でまとめて適用する
		world->GetCommandBuffer().EnqueueSetParent(child, newParent, worldPositionStays);
	}

} // Engine
