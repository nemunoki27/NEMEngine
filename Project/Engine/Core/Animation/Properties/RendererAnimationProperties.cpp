#include "BuiltinAnimationPropertyGroups.h"

//============================================================================
//	include
//============================================================================
#include "BuiltinAnimationPropertyUtility.h"
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

// c++
#include <format>

using namespace Engine::AnimationPropertyUtility;

namespace {

	// Materialの色か既定の白を取得する
	template <typename Component>
	bool GetMaterialColor(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		Component* renderer = world.TryGetComponent<Component>(entity);
		if (!renderer) {
			return false;
		}

		const Engine::MaterialParameterValue* value = renderer->materialInstance.Find(Engine::MaterialParameterIDs::BaseColor);
		if (!value) {
			out = Engine::Color4::White();
			return true;
		}
		if (const Engine::Color4* color = std::get_if<Engine::Color4>(&value->value)) {
			out = *color;
			return true;
		}
		if (const Engine::Vector4* color = std::get_if<Engine::Vector4>(&value->value)) {
			out = Engine::Color4(color->x, color->y, color->z, color->w);
			return true;
		}
		if (const Engine::Vector3* color = std::get_if<Engine::Vector3>(&value->value)) {
			out = Engine::Color4(color->x, color->y, color->z, 1.0f);
			return true;
		}
		return false;
	}

	// Materialの色を変更する
	template <typename Component>
	bool SetMaterialColor(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Engine::Color4 color{};
		if (!ReadVariant(value, color)) {
			return false;
		}
		if (Component* renderer = world.TryGetComponent<Component>(entity)) {
			Engine::MaterialParameterValue parameter{};
			parameter.value = color;
			renderer->materialInstance.Set(Engine::MaterialParameterIDs::BaseColor, Engine::MaterialParameterNames::BaseColor,
				Engine::MaterialParameterSemantic::BaseColor, parameter);
			world.MarkComponentModified<Component>(entity);
			return true;
		}
		return false;
	}

	// Spriteの大きさを取得する
	bool GetSpriteSize(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			out = renderer->size;
			return true;
		}
		return false;
	}

	// Spriteの大きさを変更する
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

	// Spriteの原点を取得する
	bool GetSpritePivot(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			out = renderer->pivot;
			return true;
		}
		return false;
	}

	// Spriteの原点を変更する
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

	// 文字配置を変更して再構築を予約する
	template <typename Value, Value Engine::TextRendererComponent::* Member>
	bool SetTextLayoutMember(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::TextRendererComponent* renderer = world.TryGetComponent<Engine::TextRendererComponent>(entity)) {
			renderer->*Member = typed;
			// レイアウトに関わる値を変えたら、次回描画時にGlyph配置を作り直す
			Engine::InvalidateTextLayout(world, entity);
			return true;
		}
		return false;
	}

	// 対象のSubMeshが存在するか確認する
	template <size_t SubMeshIndex>
	bool HasMeshSubMesh(Engine::ECSWorld& world, const Engine::Entity& entity) {

		return world.HasComponent<Engine::MeshRendererComponent>(entity) &&
			   SubMeshIndex < Engine::GetMeshSubMeshes(world, entity).size();
	}

	// SubMeshの値を取得する
	template <size_t SubMeshIndex, typename Value, Value Engine::SubMeshMaterial::* Member>
	bool GetSubMeshMember(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		const std::span<Engine::SubMeshMaterial> subMeshes = Engine::GetMeshSubMeshes(world, entity);
		if (SubMeshIndex < subMeshes.size()) {
			out = subMeshes[SubMeshIndex].*Member;
			return true;
		}
		return false;
	}

	// SubMeshのUV設定を変更する
	template <size_t SubMeshIndex, typename Value, Value Engine::SubMeshMaterial::* Member>
	bool SetSubMeshUVMember(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		const std::span<Engine::SubMeshMaterial> subMeshes = Engine::GetMeshSubMeshes(world, entity);
		if (SubMeshIndex < subMeshes.size()) {
			subMeshes[SubMeshIndex].*Member = typed;
			return true;
		}
		return false;
	}

	// SubMeshのUV設定を登録する
	template <size_t SubMeshIndex>
	void RegisterMeshSubMeshProperties(Engine::AnimationPropertyRegistry& registry) {

		const std::string prefix = std::format("subMeshes[{}]", SubMeshIndex);
		const std::string displayPrefix = std::format("MeshRenderer.subMeshes[{}]", SubMeshIndex);

		// Materialの値はreflectionから解決してUVだけを登録する
		Register(registry, "MeshRenderer", std::format("{}.uvPos", prefix).c_str(),
			std::format("{}.uvPos", displayPrefix).c_str(), Engine::AnimationValueType::Vector2, HasMeshSubMesh<SubMeshIndex>,
			GetSubMeshMember<SubMeshIndex, Engine::Vector2, &Engine::SubMeshMaterial::uvPos>,
			SetSubMeshUVMember<SubMeshIndex, Engine::Vector2, &Engine::SubMeshMaterial::uvPos>);
		Register(registry, "MeshRenderer", std::format("{}.uvRotation", prefix).c_str(),
			std::format("{}.uvRotation", displayPrefix).c_str(), Engine::AnimationValueType::Float,
			HasMeshSubMesh<SubMeshIndex>, GetSubMeshMember<SubMeshIndex, float, &Engine::SubMeshMaterial::uvRotation>,
			SetSubMeshUVMember<SubMeshIndex, float, &Engine::SubMeshMaterial::uvRotation>);
		Register(registry, "MeshRenderer", std::format("{}.uvScale", prefix).c_str(),
			std::format("{}.uvScale", displayPrefix).c_str(), Engine::AnimationValueType::Vector2, HasMeshSubMesh<SubMeshIndex>,
			GetSubMeshMember<SubMeshIndex, Engine::Vector2, &Engine::SubMeshMaterial::uvScale>,
			SetSubMeshUVMember<SubMeshIndex, Engine::Vector2, &Engine::SubMeshMaterial::uvScale>);
	}
} // namespace

// Spriteの編集値を登録する
void Engine::RegisterSpriteAnimationProperties(AnimationPropertyRegistry& registry) {

	Register(registry, "SpriteRenderer", "size", "SpriteRenderer.size", AnimationValueType::Vector2,
		HasComponent<SpriteRendererComponent>, GetSpriteSize, SetSpriteSize);
	Register(registry, "SpriteRenderer", "pivot", "SpriteRenderer.pivot", AnimationValueType::Vector2,
		HasComponent<SpriteRendererComponent>, GetSpritePivot, SetSpritePivot);
	Register(registry, "SpriteRenderer", "color", "SpriteRenderer.color", AnimationValueType::Color4,
		HasComponent<SpriteRendererComponent>, GetMaterialColor<SpriteRendererComponent>,
		SetMaterialColor<SpriteRendererComponent>);
}

// Textの編集値を登録する
void Engine::RegisterTextAnimationProperties(AnimationPropertyRegistry& registry) {

	Register(registry, "TextRenderer", "fontSize", "TextRenderer.fontSize", AnimationValueType::Float,
		HasComponent<TextRendererComponent>, GetMember<TextRendererComponent, float, &TextRendererComponent::fontSize>,
		SetTextLayoutMember<float, &TextRendererComponent::fontSize>);
	Register(registry, "TextRenderer", "charSpacing", "TextRenderer.charSpacing", AnimationValueType::Float,
		HasComponent<TextRendererComponent>, GetMember<TextRendererComponent, float, &TextRendererComponent::charSpacing>,
		SetTextLayoutMember<float, &TextRendererComponent::charSpacing>);
	Register(registry, "TextRenderer", "color", "TextRenderer.color", AnimationValueType::Color4,
		HasComponent<TextRendererComponent>, GetMaterialColor<TextRendererComponent>, SetMaterialColor<TextRendererComponent>);
}

// MeshUVの編集値を登録する
void Engine::RegisterMeshUVAnimationProperties(AnimationPropertyRegistry& registry) {

	RegisterMeshSubMeshProperties<0>(registry);
	RegisterMeshSubMeshProperties<1>(registry);
	RegisterMeshSubMeshProperties<2>(registry);
	RegisterMeshSubMeshProperties<3>(registry);
	RegisterMeshSubMeshProperties<4>(registry);
	RegisterMeshSubMeshProperties<5>(registry);
	RegisterMeshSubMeshProperties<6>(registry);
	RegisterMeshSubMeshProperties<7>(registry);
}
