#include "AnimationPropertyRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

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

	template <typename Component, typename Value, Value Component::* Member>
	bool GetMember(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Component* component = world.TryGetComponent<Component>(entity)) {
			out = component->*Member;
			return true;
		}
		return false;
	}

	template <typename Component, typename Value, Value Component::* Member>
	bool SetMember(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Component* component = world.TryGetComponent<Component>(entity)) {
			component->*Member = typed;
			return true;
		}
		return false;
	}

	template <typename Component, typename Child, typename Value, Child Component::* ChildMember, Value Child::* Member>
	bool GetChildMember(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Component* component = world.TryGetComponent<Component>(entity)) {
			out = (component->*ChildMember).*Member;
			return true;
		}
		return false;
	}

	template <typename Component, typename Child, typename Value, Child Component::* ChildMember, Value Child::* Member>
	bool SetChildMember(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Component* component = world.TryGetComponent<Component>(entity)) {
			(component->*ChildMember).*Member = typed;
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

	//============================================================================
	//	TextRendererComponent
	//============================================================================

	template <typename Value, Value Engine::TextRendererComponent::* Member>
	bool SetTextLayoutMember(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TextRendererComponent* renderer = world.TryGetComponent<Engine::TextRendererComponent>(entity)) {
			renderer->*Member = typed;
			// レイアウトに関わる値を変えたら、次回描画時にGlyph配置を作り直す。
			renderer->runtimeLayout.valid = false;
			return true;
		}
		return false;
	}

	//============================================================================
	//	CollisionComponent
	//============================================================================

	template <size_t ShapeIndex>
	bool HasCollisionShape(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity)) {
			return ShapeIndex < collision->shapes.size();
		}
		return false;
	}

	template <size_t ShapeIndex, typename Value, Value Engine::CollisionShape::* Member>
	bool GetCollisionShapeMember(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity)) {
			if (ShapeIndex < collision->shapes.size()) {
				out = collision->shapes[ShapeIndex].*Member;
				return true;
			}
		}
		return false;
	}

	template <size_t ShapeIndex, typename Value, Value Engine::CollisionShape::* Member>
	bool SetCollisionShapeMember(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity)) {
			if (ShapeIndex < collision->shapes.size()) {
				collision->shapes[ShapeIndex].*Member = typed;
				return true;
			}
		}
		return false;
	}

	//============================================================================
	//	MeshRendererComponent
	//============================================================================

	template <size_t SubMeshIndex>
	bool HasMeshSubMesh(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (Engine::MeshRendererComponent* renderer = world.TryGetComponent<Engine::MeshRendererComponent>(entity)) {
			return SubMeshIndex < renderer->subMeshes.size();
		}
		return false;
	}

	template <size_t SubMeshIndex, typename Value, Value Engine::MeshSubMeshTextureOverride::* Member>
	bool GetSubMeshMember(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::MeshRendererComponent* renderer = world.TryGetComponent<Engine::MeshRendererComponent>(entity)) {
			if (SubMeshIndex < renderer->subMeshes.size()) {
				out = renderer->subMeshes[SubMeshIndex].*Member;
				return true;
			}
		}
		return false;
	}

	template <size_t SubMeshIndex, typename Value, Value Engine::MeshSubMeshTextureOverride::* Member>
	bool SetSubMeshMember(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::MeshRendererComponent* renderer = world.TryGetComponent<Engine::MeshRendererComponent>(entity)) {
			if (SubMeshIndex < renderer->subMeshes.size()) {
				renderer->subMeshes[SubMeshIndex].*Member = typed;
				return true;
			}
		}
		return false;
	}

	template <size_t SubMeshIndex, typename Value, Value Engine::MeshSubMeshTextureOverride::* Member>
	bool SetSubMeshUVMember(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::MeshRendererComponent* renderer = world.TryGetComponent<Engine::MeshRendererComponent>(entity)) {
			if (SubMeshIndex < renderer->subMeshes.size()) {
				Engine::MeshSubMeshTextureOverride& subMesh = renderer->subMeshes[SubMeshIndex];
				subMesh.*Member = typed;
				subMesh.uvMatrix = Engine::MeshSubMeshRuntime::BuildUVMatrix(subMesh);
				return true;
			}
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

	template <size_t ShapeIndex>
	void RegisterCollisionShapeProperties(Engine::AnimationPropertyRegistry& registry) {

		const std::string prefix = std::format("shapes[{}]", ShapeIndex);
		const std::string displayPrefix = std::format("Collision.shapes[{}]", ShapeIndex);

		Register(registry, "Collision", std::format("{}.offset", prefix).c_str(),
			std::format("{}.offset", displayPrefix).c_str(), Engine::AnimationValueType::Vector3,
			HasCollisionShape<ShapeIndex>,
			GetCollisionShapeMember<ShapeIndex, Engine::Vector3, &Engine::CollisionShape::offset>,
			SetCollisionShapeMember<ShapeIndex, Engine::Vector3, &Engine::CollisionShape::offset>);
		Register(registry, "Collision", std::format("{}.rotationDegrees", prefix).c_str(),
			std::format("{}.rotationDegrees", displayPrefix).c_str(), Engine::AnimationValueType::Vector3,
			HasCollisionShape<ShapeIndex>,
			GetCollisionShapeMember<ShapeIndex, Engine::Vector3, &Engine::CollisionShape::rotationDegrees>,
			SetCollisionShapeMember<ShapeIndex, Engine::Vector3, &Engine::CollisionShape::rotationDegrees>);
		Register(registry, "Collision", std::format("{}.radius", prefix).c_str(),
			std::format("{}.radius", displayPrefix).c_str(), Engine::AnimationValueType::Float,
			HasCollisionShape<ShapeIndex>,
			GetCollisionShapeMember<ShapeIndex, float, &Engine::CollisionShape::radius>,
			SetCollisionShapeMember<ShapeIndex, float, &Engine::CollisionShape::radius>);
		Register(registry, "Collision", std::format("{}.halfSize2D", prefix).c_str(),
			std::format("{}.halfSize2D", displayPrefix).c_str(), Engine::AnimationValueType::Vector2,
			HasCollisionShape<ShapeIndex>,
			GetCollisionShapeMember<ShapeIndex, Engine::Vector2, &Engine::CollisionShape::halfSize2D>,
			SetCollisionShapeMember<ShapeIndex, Engine::Vector2, &Engine::CollisionShape::halfSize2D>);
		Register(registry, "Collision", std::format("{}.halfExtents3D", prefix).c_str(),
			std::format("{}.halfExtents3D", displayPrefix).c_str(), Engine::AnimationValueType::Vector3,
			HasCollisionShape<ShapeIndex>,
			GetCollisionShapeMember<ShapeIndex, Engine::Vector3, &Engine::CollisionShape::halfExtents3D>,
			SetCollisionShapeMember<ShapeIndex, Engine::Vector3, &Engine::CollisionShape::halfExtents3D>);
	}

	template <size_t SubMeshIndex>
	void RegisterMeshSubMeshProperties(Engine::AnimationPropertyRegistry& registry) {

		const std::string prefix = std::format("subMeshes[{}]", SubMeshIndex);
		const std::string displayPrefix = std::format("MeshRenderer.subMeshes[{}]", SubMeshIndex);

		Register(registry, "MeshRenderer", std::format("{}.color", prefix).c_str(),
			std::format("{}.color", displayPrefix).c_str(), Engine::AnimationValueType::Color4,
			HasMeshSubMesh<SubMeshIndex>,
			GetSubMeshMember<SubMeshIndex, Engine::Color4, &Engine::MeshSubMeshTextureOverride::color>,
			SetSubMeshMember<SubMeshIndex, Engine::Color4, &Engine::MeshSubMeshTextureOverride::color>);
		Register(registry, "MeshRenderer", std::format("{}.uvPos", prefix).c_str(),
			std::format("{}.uvPos", displayPrefix).c_str(), Engine::AnimationValueType::Vector2,
			HasMeshSubMesh<SubMeshIndex>,
			GetSubMeshMember<SubMeshIndex, Engine::Vector2, &Engine::MeshSubMeshTextureOverride::uvPos>,
			SetSubMeshUVMember<SubMeshIndex, Engine::Vector2, &Engine::MeshSubMeshTextureOverride::uvPos>);
		Register(registry, "MeshRenderer", std::format("{}.uvRotation", prefix).c_str(),
			std::format("{}.uvRotation", displayPrefix).c_str(), Engine::AnimationValueType::Float,
			HasMeshSubMesh<SubMeshIndex>,
			GetSubMeshMember<SubMeshIndex, float, &Engine::MeshSubMeshTextureOverride::uvRotation>,
			SetSubMeshUVMember<SubMeshIndex, float, &Engine::MeshSubMeshTextureOverride::uvRotation>);
		Register(registry, "MeshRenderer", std::format("{}.uvScale", prefix).c_str(),
			std::format("{}.uvScale", displayPrefix).c_str(), Engine::AnimationValueType::Vector2,
			HasMeshSubMesh<SubMeshIndex>,
			GetSubMeshMember<SubMeshIndex, Engine::Vector2, &Engine::MeshSubMeshTextureOverride::uvScale>,
			SetSubMeshUVMember<SubMeshIndex, Engine::Vector2, &Engine::MeshSubMeshTextureOverride::uvScale>);
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
	//============================================================================
	//	TextRendererComponent
	//============================================================================
	{
		Register(registry, "TextRenderer", "fontSize", "TextRenderer.fontSize", AnimationValueType::Float,
			HasComponent<TextRendererComponent>,
			GetMember<TextRendererComponent, float, &TextRendererComponent::fontSize>,
			SetTextLayoutMember<float, &TextRendererComponent::fontSize>);
		Register(registry, "TextRenderer", "charSpacing", "TextRenderer.charSpacing", AnimationValueType::Float,
			HasComponent<TextRendererComponent>,
			GetMember<TextRendererComponent, float, &TextRendererComponent::charSpacing>,
			SetTextLayoutMember<float, &TextRendererComponent::charSpacing>);
		Register(registry, "TextRenderer", "color", "TextRenderer.color", AnimationValueType::Color4,
			HasComponent<TextRendererComponent>,
			GetMember<TextRendererComponent, Color4, &TextRendererComponent::color>,
			SetMember<TextRendererComponent, Color4, &TextRendererComponent::color>);
	}
	//============================================================================
	//	LightComponents
	//============================================================================
	{
		Register(registry, "DirectionalLight", "color", "DirectionalLight.color", AnimationValueType::Color4,
			HasComponent<DirectionalLightComponent>,
			GetMember<DirectionalLightComponent, Color4, &DirectionalLightComponent::color>,
			SetMember<DirectionalLightComponent, Color4, &DirectionalLightComponent::color>);
		Register(registry, "DirectionalLight", "direction", "DirectionalLight.direction", AnimationValueType::Vector3,
			HasComponent<DirectionalLightComponent>,
			GetMember<DirectionalLightComponent, Vector3, &DirectionalLightComponent::direction>,
			SetMember<DirectionalLightComponent, Vector3, &DirectionalLightComponent::direction>);
		Register(registry, "DirectionalLight", "intensity", "DirectionalLight.intensity", AnimationValueType::Float,
			HasComponent<DirectionalLightComponent>,
			GetMember<DirectionalLightComponent, float, &DirectionalLightComponent::intensity>,
			SetMember<DirectionalLightComponent, float, &DirectionalLightComponent::intensity>);

		Register(registry, "PointLight", "color", "PointLight.color", AnimationValueType::Color4,
			HasComponent<PointLightComponent>,
			GetMember<PointLightComponent, Color4, &PointLightComponent::color>,
			SetMember<PointLightComponent, Color4, &PointLightComponent::color>);
		Register(registry, "PointLight", "intensity", "PointLight.intensity", AnimationValueType::Float,
			HasComponent<PointLightComponent>,
			GetMember<PointLightComponent, float, &PointLightComponent::intensity>,
			SetMember<PointLightComponent, float, &PointLightComponent::intensity>);
		Register(registry, "PointLight", "radius", "PointLight.radius", AnimationValueType::Float,
			HasComponent<PointLightComponent>,
			GetMember<PointLightComponent, float, &PointLightComponent::radius>,
			SetMember<PointLightComponent, float, &PointLightComponent::radius>);
		Register(registry, "PointLight", "decay", "PointLight.decay", AnimationValueType::Float,
			HasComponent<PointLightComponent>,
			GetMember<PointLightComponent, float, &PointLightComponent::decay>,
			SetMember<PointLightComponent, float, &PointLightComponent::decay>);

		Register(registry, "SpotLight", "color", "SpotLight.color", AnimationValueType::Color4,
			HasComponent<SpotLightComponent>,
			GetMember<SpotLightComponent, Color4, &SpotLightComponent::color>,
			SetMember<SpotLightComponent, Color4, &SpotLightComponent::color>);
		Register(registry, "SpotLight", "direction", "SpotLight.direction", AnimationValueType::Vector3,
			HasComponent<SpotLightComponent>,
			GetMember<SpotLightComponent, Vector3, &SpotLightComponent::direction>,
			SetMember<SpotLightComponent, Vector3, &SpotLightComponent::direction>);
		Register(registry, "SpotLight", "intensity", "SpotLight.intensity", AnimationValueType::Float,
			HasComponent<SpotLightComponent>,
			GetMember<SpotLightComponent, float, &SpotLightComponent::intensity>,
			SetMember<SpotLightComponent, float, &SpotLightComponent::intensity>);
		Register(registry, "SpotLight", "distance", "SpotLight.distance", AnimationValueType::Float,
			HasComponent<SpotLightComponent>,
			GetMember<SpotLightComponent, float, &SpotLightComponent::distance>,
			SetMember<SpotLightComponent, float, &SpotLightComponent::distance>);
		Register(registry, "SpotLight", "decay", "SpotLight.decay", AnimationValueType::Float,
			HasComponent<SpotLightComponent>,
			GetMember<SpotLightComponent, float, &SpotLightComponent::decay>,
			SetMember<SpotLightComponent, float, &SpotLightComponent::decay>);
		Register(registry, "SpotLight", "cosAngle", "SpotLight.cosAngle", AnimationValueType::Float,
			HasComponent<SpotLightComponent>,
			GetMember<SpotLightComponent, float, &SpotLightComponent::cosAngle>,
			SetMember<SpotLightComponent, float, &SpotLightComponent::cosAngle>);
		Register(registry, "SpotLight", "cosFalloffStart", "SpotLight.cosFalloffStart", AnimationValueType::Float,
			HasComponent<SpotLightComponent>,
			GetMember<SpotLightComponent, float, &SpotLightComponent::cosFalloffStart>,
			SetMember<SpotLightComponent, float, &SpotLightComponent::cosFalloffStart>);
	}
	//============================================================================
	//	CameraComponents
	//============================================================================
	{
		Register(registry, "OrthographicCamera", "nearClip", "OrthographicCamera.nearClip", AnimationValueType::Float,
			HasComponent<OrthographicCameraComponent>,
			GetMember<OrthographicCameraComponent, float, &OrthographicCameraComponent::nearClip>,
			SetMember<OrthographicCameraComponent, float, &OrthographicCameraComponent::nearClip>);
		Register(registry, "OrthographicCamera", "farClip", "OrthographicCamera.farClip", AnimationValueType::Float,
			HasComponent<OrthographicCameraComponent>,
			GetMember<OrthographicCameraComponent, float, &OrthographicCameraComponent::farClip>,
			SetMember<OrthographicCameraComponent, float, &OrthographicCameraComponent::farClip>);
		Register(registry, "OrthographicCamera", "common.aspectRatio", "OrthographicCamera.aspectRatio", AnimationValueType::Float,
			HasComponent<OrthographicCameraComponent>,
			GetChildMember<OrthographicCameraComponent, CameraCommon, float, &OrthographicCameraComponent::common, &CameraCommon::aspectRatio>,
			SetChildMember<OrthographicCameraComponent, CameraCommon, float, &OrthographicCameraComponent::common, &CameraCommon::aspectRatio>);
		Register(registry, "OrthographicCamera", "common.editorFrustumScale", "OrthographicCamera.editorFrustumScale", AnimationValueType::Float,
			HasComponent<OrthographicCameraComponent>,
			GetChildMember<OrthographicCameraComponent, CameraCommon, float, &OrthographicCameraComponent::common, &CameraCommon::editorFrustumScale>,
			SetChildMember<OrthographicCameraComponent, CameraCommon, float, &OrthographicCameraComponent::common, &CameraCommon::editorFrustumScale>);

		Register(registry, "PerspectiveCamera", "fovY", "PerspectiveCamera.fovY", AnimationValueType::Float,
			HasComponent<PerspectiveCameraComponent>,
			GetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::fovY>,
			SetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::fovY>);
		Register(registry, "PerspectiveCamera", "nearClip", "PerspectiveCamera.nearClip", AnimationValueType::Float,
			HasComponent<PerspectiveCameraComponent>,
			GetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::nearClip>,
			SetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::nearClip>);
		Register(registry, "PerspectiveCamera", "farClip", "PerspectiveCamera.farClip", AnimationValueType::Float,
			HasComponent<PerspectiveCameraComponent>,
			GetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::farClip>,
			SetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::farClip>);
		Register(registry, "PerspectiveCamera", "common.aspectRatio", "PerspectiveCamera.aspectRatio", AnimationValueType::Float,
			HasComponent<PerspectiveCameraComponent>,
			GetChildMember<PerspectiveCameraComponent, CameraCommon, float, &PerspectiveCameraComponent::common, &CameraCommon::aspectRatio>,
			SetChildMember<PerspectiveCameraComponent, CameraCommon, float, &PerspectiveCameraComponent::common, &CameraCommon::aspectRatio>);
		Register(registry, "PerspectiveCamera", "common.editorFrustumScale", "PerspectiveCamera.editorFrustumScale", AnimationValueType::Float,
			HasComponent<PerspectiveCameraComponent>,
			GetChildMember<PerspectiveCameraComponent, CameraCommon, float, &PerspectiveCameraComponent::common, &CameraCommon::editorFrustumScale>,
			SetChildMember<PerspectiveCameraComponent, CameraCommon, float, &PerspectiveCameraComponent::common, &CameraCommon::editorFrustumScale>);
	}
	//============================================================================
	//	CameraControllerComponent
	//============================================================================
	{
		Register(registry, "CameraController", "follow.offset", "CameraController.follow.offset", AnimationValueType::Vector3,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow, &CameraFollowSettings::offset>,
			SetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow, &CameraFollowSettings::offset>);
		Register(registry, "CameraController", "follow.axisMask", "CameraController.follow.axisMask", AnimationValueType::Vector3,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow, &CameraFollowSettings::axisMask>,
			SetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow, &CameraFollowSettings::axisMask>);
		Register(registry, "CameraController", "follow.positionLerpSpeed", "CameraController.follow.positionLerpSpeed", AnimationValueType::Float,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraFollowSettings, float, &CameraControllerComponent::follow, &CameraFollowSettings::positionLerpSpeed>,
			SetChildMember<CameraControllerComponent, CameraFollowSettings, float, &CameraControllerComponent::follow, &CameraFollowSettings::positionLerpSpeed>);
		Register(registry, "CameraController", "follow.boundsMin", "CameraController.follow.boundsMin", AnimationValueType::Vector3,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow, &CameraFollowSettings::boundsMin>,
			SetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow, &CameraFollowSettings::boundsMin>);
		Register(registry, "CameraController", "follow.boundsMax", "CameraController.follow.boundsMax", AnimationValueType::Vector3,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow, &CameraFollowSettings::boundsMax>,
			SetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow, &CameraFollowSettings::boundsMax>);
		Register(registry, "CameraController", "lookAt.offset", "CameraController.lookAt.offset", AnimationValueType::Vector3,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraLookAtSettings, Vector3, &CameraControllerComponent::lookAt, &CameraLookAtSettings::offset>,
			SetChildMember<CameraControllerComponent, CameraLookAtSettings, Vector3, &CameraControllerComponent::lookAt, &CameraLookAtSettings::offset>);
		Register(registry, "CameraController", "lookAt.rotationLerpSpeed", "CameraController.lookAt.rotationLerpSpeed", AnimationValueType::Float,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraLookAtSettings, float, &CameraControllerComponent::lookAt, &CameraLookAtSettings::rotationLerpSpeed>,
			SetChildMember<CameraControllerComponent, CameraLookAtSettings, float, &CameraControllerComponent::lookAt, &CameraLookAtSettings::rotationLerpSpeed>);
		Register(registry, "CameraController", "shake.amplitude", "CameraController.shake.amplitude", AnimationValueType::Float,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraShakeSettings, float, &CameraControllerComponent::shake, &CameraShakeSettings::amplitude>,
			SetChildMember<CameraControllerComponent, CameraShakeSettings, float, &CameraControllerComponent::shake, &CameraShakeSettings::amplitude>);
		Register(registry, "CameraController", "shake.duration", "CameraController.shake.duration", AnimationValueType::Float,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraShakeSettings, float, &CameraControllerComponent::shake, &CameraShakeSettings::duration>,
			SetChildMember<CameraControllerComponent, CameraShakeSettings, float, &CameraControllerComponent::shake, &CameraShakeSettings::duration>);
		Register(registry, "CameraController", "shake.frequency", "CameraController.shake.frequency", AnimationValueType::Float,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraShakeSettings, float, &CameraControllerComponent::shake, &CameraShakeSettings::frequency>,
			SetChildMember<CameraControllerComponent, CameraShakeSettings, float, &CameraControllerComponent::shake, &CameraShakeSettings::frequency>);
		Register(registry, "CameraController", "shake.damping", "CameraController.shake.damping", AnimationValueType::Float,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraShakeSettings, float, &CameraControllerComponent::shake, &CameraShakeSettings::damping>,
			SetChildMember<CameraControllerComponent, CameraShakeSettings, float, &CameraControllerComponent::shake, &CameraShakeSettings::damping>);
		Register(registry, "CameraController", "shake.axisMask", "CameraController.shake.axisMask", AnimationValueType::Vector3,
			HasComponent<CameraControllerComponent>,
			GetChildMember<CameraControllerComponent, CameraShakeSettings, Vector3, &CameraControllerComponent::shake, &CameraShakeSettings::axisMask>,
			SetChildMember<CameraControllerComponent, CameraShakeSettings, Vector3, &CameraControllerComponent::shake, &CameraShakeSettings::axisMask>);
	}
	//============================================================================
	//	Audio / UV / SkinnedAnimation
	//============================================================================
	{
		Register(registry, "AudioSource", "volume", "AudioSource.volume", AnimationValueType::Float,
			HasComponent<AudioSourceComponent>,
			GetMember<AudioSourceComponent, float, &AudioSourceComponent::volume>,
			SetMember<AudioSourceComponent, float, &AudioSourceComponent::volume>);
		Register(registry, "UVTransform", "pos", "UVTransform.pos", AnimationValueType::Vector2,
			HasComponent<UVTransformComponent>,
			GetMember<UVTransformComponent, Vector2, &UVTransformComponent::pos>,
			SetMember<UVTransformComponent, Vector2, &UVTransformComponent::pos>);
		Register(registry, "UVTransform", "rotation", "UVTransform.rotation", AnimationValueType::Float,
			HasComponent<UVTransformComponent>,
			GetMember<UVTransformComponent, float, &UVTransformComponent::rotation>,
			SetMember<UVTransformComponent, float, &UVTransformComponent::rotation>);
		Register(registry, "UVTransform", "scale", "UVTransform.scale", AnimationValueType::Vector2,
			HasComponent<UVTransformComponent>,
			GetMember<UVTransformComponent, Vector2, &UVTransformComponent::scale>,
			SetMember<UVTransformComponent, Vector2, &UVTransformComponent::scale>);
		Register(registry, "SkinnedAnimation", "playbackSpeed", "SkinnedAnimation.playbackSpeed", AnimationValueType::Float,
			HasComponent<SkinnedAnimationComponent>,
			GetMember<SkinnedAnimationComponent, float, &SkinnedAnimationComponent::playbackSpeed>,
			SetMember<SkinnedAnimationComponent, float, &SkinnedAnimationComponent::playbackSpeed>);
		Register(registry, "SkinnedAnimation", "transitionDuration", "SkinnedAnimation.transitionDuration", AnimationValueType::Float,
			HasComponent<SkinnedAnimationComponent>,
			GetMember<SkinnedAnimationComponent, float, &SkinnedAnimationComponent::transitionDuration>,
			SetMember<SkinnedAnimationComponent, float, &SkinnedAnimationComponent::transitionDuration>);
	}
	//============================================================================
	//	CollisionComponent
	//============================================================================
	{
		RegisterCollisionShapeProperties<0>(registry);
		RegisterCollisionShapeProperties<1>(registry);
		RegisterCollisionShapeProperties<2>(registry);
		RegisterCollisionShapeProperties<3>(registry);
		RegisterCollisionShapeProperties<4>(registry);
		RegisterCollisionShapeProperties<5>(registry);
		RegisterCollisionShapeProperties<6>(registry);
		RegisterCollisionShapeProperties<7>(registry);
	}
	//============================================================================
	//	MeshRendererComponent
	//============================================================================
	{
		RegisterMeshSubMeshProperties<0>(registry);
		RegisterMeshSubMeshProperties<1>(registry);
		RegisterMeshSubMeshProperties<2>(registry);
		RegisterMeshSubMeshProperties<3>(registry);
		RegisterMeshSubMeshProperties<4>(registry);
		RegisterMeshSubMeshProperties<5>(registry);
		RegisterMeshSubMeshProperties<6>(registry);
		RegisterMeshSubMeshProperties<7>(registry);
	}
}
