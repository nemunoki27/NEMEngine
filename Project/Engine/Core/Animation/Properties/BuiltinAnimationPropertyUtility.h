#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationPropertyRegistry.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::AnimationPropertyUtility {

	// 対象のComponentを持つか確認する
	template <typename Component>
	bool HasComponent(Engine::ECSWorld& world, const Engine::Entity& entity) {

		return world.HasComponent<Component>(entity);
	}

	// Propertyの値を指定の型で取得する
	template <typename Value>
	bool ReadVariant(const Engine::AnimationPropertyValue& value, Value& out) {

		// PropertyDescriptorのvalueTypeと実データ型が違う場合は適用しない
		if (const Value* typed = std::get_if<Value>(&value)) {
			out = *typed;
			return true;
		}
		return false;
	}

	// Componentの値を取得する
	template <typename Component, typename Value, Value Component::* Member>
	bool GetMember(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Component* component = world.TryGetComponent<Component>(entity)) {
			out = component->*Member;
			return true;
		}
		return false;
	}

	// Componentの値を変更して通知する
	template <typename Component, typename Value, Value Component::* Member>
	bool SetMember(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Component* component = world.TryGetComponent<Component>(entity)) {
			component->*Member = typed;
			world.MarkComponentModified<Component>(entity);
			return true;
		}
		return false;
	}

	// Component内の設定値を取得する
	template <typename Component, typename Child, typename Value, Child Component::* ChildMember, Value Child::* Member>
	bool GetChildMember(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Component* component = world.TryGetComponent<Component>(entity)) {
			out = (component->*ChildMember).*Member;
			return true;
		}
		return false;
	}

	// Component内の設定値を変更して通知する
	template <typename Component, typename Child, typename Value, Child Component::* ChildMember, Value Child::* Member>
	bool SetChildMember(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Component* component = world.TryGetComponent<Component>(entity)) {
			(component->*ChildMember).*Member = typed;
			world.MarkComponentModified<Component>(entity);
			return true;
		}
		return false;
	}

	// ToolとRuntimeの共通プロパティを登録する
	void Register(Engine::AnimationPropertyRegistry& registry, const char* componentName, const char* propertyPath,
		const char* displayName, Engine::AnimationValueType valueType,
		bool (*hasComponent)(Engine::ECSWorld&, const Engine::Entity&),
		bool (*getValue)(Engine::ECSWorld&, const Engine::Entity&, Engine::AnimationPropertyValue&),
		bool (*setValue)(Engine::ECSWorld&, const Engine::Entity&, const Engine::AnimationPropertyValue&));
} // Engine::AnimationPropertyUtility
