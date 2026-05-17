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

		// PropertyDescriptorのvalueTypeと実データ型が違う場合は適用しない。
		if (const Value* typed = std::get_if<Value>(&value)) {
			out = *typed;
			return true;
		}
		return false;
	}

	//============================================================================
	//	TransformComponent
	//============================================================================

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
	bool GetTransformLocalPos2D(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = Engine::Vector2(transform->localPos.x, transform->localPos.y);
			return true;
		}
		return false;
	}
	bool SetTransformLocalPos2D(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector2 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			// 2D編集ではZを保持したままXYだけをAnimationClipから上書きする。
			transform->localPos.x = typed.x;
			transform->localPos.y = typed.y;
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
	bool GetTransformLocalRotationZ(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = Engine::Quaternion::ToEulerDegrees(transform->localRotation).z;
			return true;
		}
		return false;
	}
	bool SetTransformLocalRotationZ(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		float typed = 0.0f;
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			// 2D編集用に、既存のX/Y回転は残してZ回転だけ差し替える。
			Engine::Vector3 euler = Engine::Quaternion::ToEulerDegrees(transform->localRotation);
			euler.z = typed;
			transform->localRotation = Engine::Quaternion::FromEulerDegrees(euler);
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
	bool GetTransformLocalScale2D(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			out = Engine::Vector2(transform->localScale.x, transform->localScale.y);
			return true;
		}
		return false;
	}
	bool SetTransformLocalScale2D(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Vector2 typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TransformComponent* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

			// 2D編集ではZ Scaleを保持して、Sprite系の見た目に必要なXYだけ動かす。
			transform->localScale.x = typed.x;
			transform->localScale.y = typed.y;
			Engine::MarkTransformSubtreeDirty(world, entity);
			return true;
		}
		return false;
	}

	//============================================================================
	//	SpriteRendererComponent
	//============================================================================

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

	void Register(Engine::AnimationPropertyRegistry& registry, const char* componentName, const char* propertyPath,
		const char* displayName, Engine::AnimationValueType valueType, bool (*hasComponent)(Engine::ECSWorld&, const Engine::Entity&),
		bool (*getValue)(Engine::ECSWorld&, const Engine::Entity&, Engine::AnimationPropertyValue&),
		bool (*setValue)(Engine::ECSWorld&, const Engine::Entity&, const Engine::AnimationPropertyValue&)) {

		// ToolとRuntimeの両方から同じDescriptorを引けるよう、登録情報を一箇所に集約する。
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

	// EditorToolが複数回生成されても、同じPropertyを重複登録しない
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	AnimationPropertyRegistry& registry = AnimationPropertyRegistry::GetInstance();

	//============================================================================
	//	TransformComponent
	//============================================================================
	{
		Register(registry, "Transform", "localPos", "Transform.localPos", AnimationValueType::Vector3,
			HasComponent<TransformComponent>, GetTransformLocalPos, SetTransformLocalPos);
		Register(registry, "Transform", "localPos2D", "Transform.localPos2D", AnimationValueType::Vector2,
			HasComponent<TransformComponent>, GetTransformLocalPos2D, SetTransformLocalPos2D);
		Register(registry, "Transform", "localRotation", "Transform.localRotation", AnimationValueType::Quaternion,
			HasComponent<TransformComponent>, GetTransformLocalRotation, SetTransformLocalRotation);
		Register(registry, "Transform", "localRotationZ", "Transform.localRotationZ", AnimationValueType::Float,
			HasComponent<TransformComponent>, GetTransformLocalRotationZ, SetTransformLocalRotationZ);
		Register(registry, "Transform", "localScale", "Transform.localScale", AnimationValueType::Vector3,
			HasComponent<TransformComponent>, GetTransformLocalScale, SetTransformLocalScale);
		Register(registry, "Transform", "localScale2D", "Transform.localScale2D", AnimationValueType::Vector2,
			HasComponent<TransformComponent>, GetTransformLocalScale2D, SetTransformLocalScale2D);
	}
	//============================================================================
	//	SpriteRendererComponent
	//============================================================================
	{
		Register(registry, "SpriteRenderer", "size", "SpriteRenderer.size", AnimationValueType::Vector2,
			HasComponent<SpriteRendererComponent>, GetSpriteSize, SetSpriteSize);
		Register(registry, "SpriteRenderer", "pivot", "SpriteRenderer.pivot", AnimationValueType::Vector2,
			HasComponent<SpriteRendererComponent>, GetSpritePivot, SetSpritePivot);
		Register(registry, "SpriteRenderer", "color", "SpriteRenderer.color", AnimationValueType::Color4,
			HasComponent<SpriteRendererComponent>, GetSpriteColor, SetSpriteColor);
	}
}