#include "AnimationPropertyRegistry.h"
#include "MaterialAnimationPropertyCatalog.h"
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <unordered_map>

//============================================================================
//	AnimationPropertyRegistry classMethods
//============================================================================
Engine::AnimationPropertyRegistry& Engine::AnimationPropertyRegistry::GetInstance() {

	static AnimationPropertyRegistry registry;
	return registry;
}

void Engine::AnimationPropertyRegistry::Register(const AnimationPropertyDescriptor& desc) {

	if (desc.componentName.empty() || desc.propertyPath.empty()) {
		return;
	}

	// 同じComponent.Propertyは上書き登録しない
	// Builtin登録を複数回呼んでも、ツール側のAddProperty一覧が重複しないようにしている
	if (Find(desc.componentName, desc.propertyPath)) {
		return;
	}
	properties_.emplace_back(desc);
}

void Engine::AnimationPropertyRegistry::RegisterMaterialAccessor(const MaterialAnimationAccessor& accessor) {

	if (accessor.componentName.empty() || !accessor.enumerate || !accessor.resolve) {
		return;
	}
	// 同じComponentのアクセサは重複登録しない
	for (const MaterialAnimationAccessor& registered : materialAccessors_) {
		if (registered.componentName == accessor.componentName) {
			return;
		}
	}
	materialAccessors_.emplace_back(accessor);
}

const Engine::AnimationPropertyDescriptor* Engine::AnimationPropertyRegistry::Find(
	std::string_view componentName, std::string_view propertyPath) const {

	auto it = std::find_if(properties_.begin(), properties_.end(),
		[componentName, propertyPath](const AnimationPropertyDescriptor& desc) {
			return desc.componentName == componentName && desc.propertyPath == propertyPath;
		});
	return it != properties_.end() ? &(*it) : nullptr;
}

std::vector<Engine::AnimationPropertyDescriptor> Engine::AnimationPropertyRegistry::CollectProperties(
	ECSWorld& world, const Entity& entity, const AnimationPropertyQueryContext& context) const {

	std::vector<AnimationPropertyDescriptor> result{};
	SetAnimationMaterialContext(world, context.assetDatabase,
		context.renderPipeline ? &context.renderPipeline->GetRenderAssetLibrary() : nullptr);
	if (!world.IsAlive(entity)) {
		return result;
	}

	// 静的に登録されたプロパティ
	for (const AnimationPropertyDescriptor& desc : properties_) {
		if (!desc.hasComponent || !desc.hasComponent(world, entity)) {
			continue;
		}
		result.emplace_back(desc);
	}

	// 個別マテリアルパラメータはreflection駆動で動的に列挙する
	for (const MaterialAnimationAccessor& accessor : materialAccessors_) {
		accessor.enumerate(context, world, entity, result);
	}

	// 色の旧名とマテリアル参照を同じ適用先としてまとめる
	std::vector<AnimationPropertyDescriptor> unique;
	std::unordered_map<std::string, size_t> indices;
	for (auto& desc : result) {
		std::string path = desc.propertyPath;
		if ((desc.componentName == "SpriteRenderer" || desc.componentName == "TextRenderer") && path == "color") {
			path = "material.color";
			if (auto resolved = ResolveProperty(world, entity, desc.componentName, path, desc.valueType)) desc = std::move(*resolved);
		}
		const std::string key = desc.componentName + "." + path;
		const auto [it, inserted] = indices.emplace(key, unique.size());
		if (inserted) {
			unique.emplace_back(std::move(desc));
		} else if (desc.propertyPath == path) {
			unique[it->second] = std::move(desc);
		}
	}
	return unique;
}

std::optional<Engine::AnimationPropertyDescriptor> Engine::AnimationPropertyRegistry::ResolveProperty(
	ECSWorld& world, const Entity& entity, std::string_view componentName,
	std::string_view propertyPath, AnimationValueType valueType) const {

	if ((componentName == "SpriteRenderer" || componentName == "TextRenderer") && propertyPath == "color") {
		propertyPath = "material.color";
	}

	// 静的に登録済みならそのまま使う
	if (const AnimationPropertyDescriptor* desc = Find(componentName, propertyPath)) {
		return *desc;
	}

	// 動的なマテリアルパラメータはアクセサへ委ねる
	for (const MaterialAnimationAccessor& accessor : materialAccessors_) {
		if (accessor.componentName != componentName) {
			continue;
		}
		return accessor.resolve(world, entity, propertyPath, valueType);
	}
	return std::nullopt;
}
