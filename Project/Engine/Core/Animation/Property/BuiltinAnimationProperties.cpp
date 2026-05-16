#include "AnimationPropertyRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/SpriteRendererComponent.h>

//============================================================================
//	BuiltinAnimationProperties functions
//============================================================================

namespace {

	template <typename Component>
	bool HasComponent(Engine::ECSWorld& world, const Engine::Entity& entity) {

		return world.HasComponent<Component>(entity);
	}

	template <typename Value>
	bool ReadVariant(const Engine::AnimationPropertyValue& value, Value& out) {

		if (const Value* typed = std::get_if<Value>(&value)) {
			out = *typed;
			return true;
		}
		return false;
	}

	bool GetTransformLocalPos(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = transform->localPos;
			return true;
		}
		return false;
	}

	bool SetTransformLocalPos(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector3 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			transform->localPos = typed;
			// 親子階層を持つ場合、子のworldMatrixも再計算対象にする必要がある。
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	bool GetTransformLocalRotation(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = transform->localRotation;
			return true;
		}
		return false;
	}

	bool SetTransformLocalRotation(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Quaternion typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			transform->localRotation = Engine::Quaternion::Normalize(typed);
			// Quaternionは4ch保存だが、適用時は正規化して行列生成の誤差を抑える。
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	bool GetTransformLocalScale(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = transform->localScale;
			return true;
		}
		return false;
	}

	bool SetTransformLocalScale(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector3 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			transform->localScale = typed;
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	bool GetSpriteSize(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			out = renderer->size;
			return true;
		}
		return false;
	}

	bool SetSpriteSize(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector2 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			renderer->size = typed;
			return true;
		}
		return false;
	}

	bool GetSpritePivot(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			out = renderer->pivot;
			return true;
		}
		return false;
	}

	bool SetSpritePivot(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector2 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			renderer->pivot = typed;
			return true;
		}
		return false;
	}

	bool GetSpriteColor(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			out = renderer->color;
			return true;
		}
		return false;
	}

	bool SetSpriteColor(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Color4 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			renderer->color = typed;
			return true;
		}
		return false;
	}

	void Register(Engine::AnimationPropertyRegistry& registry,
		const char* componentName,
		const char* propertyPath,
		const char* displayName,
		Engine::AnimationValueType valueType,
		bool (*hasComponent)(Engine::ECSWorld&, const Engine::Entity&),
		bool (*getValue)(Engine::ECSWorld&, const Engine::Entity&, Engine::AnimationPropertyValue&),
		bool (*setValue)(Engine::ECSWorld&, const Engine::Entity&, const Engine::AnimationPropertyValue&)) {

		Engine::AnimationPropertyDescriptor desc{};
		desc.componentName = componentName;
		desc.propertyPath = propertyPath;
		desc.displayName = displayName;
		desc.valueType = valueType;
		desc.hasComponent = hasComponent;
		desc.getValue = getValue;
		desc.setValue = setValue;
		registry.Register(desc);
	}
}

void Engine::RegisterBuiltinAnimationProperties() {

	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	AnimationPropertyRegistry& registry = AnimationPropertyRegistry::GetInstance();

	Register(registry, "Transform", "localPos", "Transform.localPos", AnimationValueType::Vector3,
		HasComponent<TransformComponent>, GetTransformLocalPos, SetTransformLocalPos);
	Register(registry, "Transform", "localRotation", "Transform.localRotation", AnimationValueType::Quaternion,
		HasComponent<TransformComponent>, GetTransformLocalRotation, SetTransformLocalRotation);
	Register(registry, "Transform", "localScale", "Transform.localScale", AnimationValueType::Vector3,
		HasComponent<TransformComponent>, GetTransformLocalScale, SetTransformLocalScale);

	Register(registry, "SpriteRenderer", "size", "SpriteRenderer.size", AnimationValueType::Vector2,
		HasComponent<SpriteRendererComponent>, GetSpriteSize, SetSpriteSize);
	Register(registry, "SpriteRenderer", "pivot", "SpriteRenderer.pivot", AnimationValueType::Vector2,
		HasComponent<SpriteRendererComponent>, GetSpritePivot, SetSpritePivot);
	Register(registry, "SpriteRenderer", "color", "SpriteRenderer.color", AnimationValueType::Color4,
		HasComponent<SpriteRendererComponent>, GetSpriteColor, SetSpriteColor);
}
