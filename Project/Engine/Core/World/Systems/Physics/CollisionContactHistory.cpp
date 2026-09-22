#include "CollisionContactHistory.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	Engine::CollisionContact MakeSwappedContact(const Engine::CollisionContact& contact) {

		Engine::CollisionContact swapped = contact;
		std::swap(swapped.self, swapped.other);
		std::swap(swapped.selfShapeIndex, swapped.otherShapeIndex);
		swapped.normal = -contact.normal;
		return swapped;
	}
}

void Engine::CollisionContactHistory::DispatchCollisionEnter(ECSWorld& world,
	SystemContext& context, const CollisionContact& contact) const {

	BehaviorSystem::DispatchCollisionEnter(world, context, contact);
	BehaviorSystem::DispatchCollisionEnter(world, context, MakeSwappedContact(contact));
}

void Engine::CollisionContactHistory::DispatchCollisionStay(ECSWorld& world,
	SystemContext& context, const CollisionContact& contact) const {

	BehaviorSystem::DispatchCollisionStay(world, context, contact);
	BehaviorSystem::DispatchCollisionStay(world, context, MakeSwappedContact(contact));
}

void Engine::CollisionContactHistory::DispatchCollisionExit(ECSWorld& world,
	SystemContext& context, const CollisionContact& contact) const {

	BehaviorSystem::DispatchCollisionExit(world, context, contact);
	BehaviorSystem::DispatchCollisionExit(world, context, MakeSwappedContact(contact));
}

void Engine::CollisionContactHistory::NotifyContact(ECSWorld& world, SystemContext& context, const CollisionPairKey& key, const CollisionContact& contact) const {

	if (previousContacts_.contains(key)) {
		DispatchCollisionStay(world, context, contact);
	} else {
		DispatchCollisionEnter(world, context, contact);
	}
}

void Engine::CollisionContactHistory::Commit(ECSWorld& world, SystemContext& context, Contacts currentContacts) {

	// 前フレームにだけ存在した接触はExitとして扱う
	for (const auto& [key, contact] : previousContacts_) {
		if (currentContacts.contains(key)) {
			continue;
		}
		// 破棄済みEntityはExitで死んだハンドルをスクリプトへ渡さないよう対象外にする
		if (!world.IsAlive(contact.self) || !world.IsAlive(contact.other)) {
			continue;
		}
		DispatchCollisionExit(world, context, contact);
	}
	previousContacts_ = std::move(currentContacts);
}
