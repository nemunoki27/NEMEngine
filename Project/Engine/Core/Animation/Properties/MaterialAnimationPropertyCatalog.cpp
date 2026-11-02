#include "MaterialAnimationPropertyCatalog.h"
#include "MaterialAnimationPropertyValue.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <memory>
#include <type_traits>

namespace {

	struct AnimationMaterialContext {
		std::unique_ptr<Engine::RenderAssetLibrary> fallback;
		Engine::AssetDatabase* database = nullptr;
		Engine::RenderAssetLibrary* library = nullptr;
		uint64_t contentRevision = 0;
	};

	// 成分数と色の指定から値型を決める
	Engine::AnimationValueType ResolveAnimationValueType(const Engine::ShaderConstantBufferVariable& var) {

		const uint32_t count = Engine::GetVariableComponentCount(var);
		switch (count) {
		case 1:
			return Engine::AnimationValueType::Float;
		case 2:
			return Engine::AnimationValueType::Vector2;
		case 3:
			return var.isColor ? Engine::AnimationValueType::Color3 : Engine::AnimationValueType::Vector3;
		default:
			return var.isColor ? Engine::AnimationValueType::Color4 : Engine::AnimationValueType::Vector4;
		}
	}

	// 使用するfloat型の変数を列挙する
	void CollectFloatFamilyParams(const Engine::ShaderReflectionInfo& reflection, const std::string& cbufferName,
		std::vector<std::pair<std::string, Engine::AnimationValueType>>& out) {

		for (const Engine::ShaderConstantBufferInfo& cb : reflection.constantBuffers) {
			if (cb.name != cbufferName) {
				continue;
			}
			for (const Engine::ShaderConstantBufferVariable& var : cb.variables) {

				if (var.valueType != D3D_SVT_FLOAT || !var.used) {
					continue;
				}
				out.emplace_back(var.name, ResolveAnimationValueType(var));
			}
		}
	}

	// 編集対象のDrawパスを取得する
	const Engine::ShaderReflectionInfo* GetMaterialDrawReflection(
		const Engine::AnimationPropertyQueryContext& context, Engine::AssetID materialID) {

		if (!context.assetDatabase || !context.renderPipeline || !materialID) {
			return nullptr;
		}
		const auto* material = context.renderPipeline->GetRenderAssetLibrary().LoadMaterial(materialID);
		return material ? context.renderPipeline->FindMaterialDrawReflection(*material) : nullptr;
	}
}

void Engine::SetAnimationMaterialContext(ECSWorld& world, AssetDatabase* database, RenderAssetLibrary* library) {

	auto& context = world.GetStorage().Get<AnimationMaterialContext>();
	if (library || !database) {
		context.library = library;
		return;
	}
	if (!context.fallback || context.database != database) {
		context.fallback = std::make_unique<RenderAssetLibrary>();
		context.fallback->Init(database);
		context.database = database;
		context.contentRevision = database->GetContentRevision();
	}
	if (context.contentRevision != database->GetContentRevision()) {
		context.fallback->Clear();
		context.contentRevision = database->GetContentRevision();
	}
	context.library = context.fallback.get();
}

bool Engine::ReadAnimationMaterialDefault(ECSWorld& world, AssetID material, const std::string& name,
	AnimationValueType type, AnimationPropertyValue& value) {

	auto& context = world.GetStorage().Get<AnimationMaterialContext>();
	if (!context.library || !material) return false;
	const auto* asset = context.library->LoadMaterial(material);
	if (!asset) return false;
	const auto found = asset->parameters.find(name);
	return found != asset->parameters.end() && MaterialValueToAnimation(found->second, type, value);
}

std::vector<std::pair<std::string, Engine::AnimationValueType>> Engine::CollectMaterialAnimationParameters(
	ECSWorld& world, const AnimationPropertyQueryContext& context, AssetID materialID, const std::string& constantBufferName) {

	// Materialが参照するDrawパスの型情報を取得する
	const ShaderReflectionInfo* reflection = GetMaterialDrawReflection(context, materialID);

	// 未使用の変数とTexture参照を除く
	std::vector<std::pair<std::string, AnimationValueType>> parameters;
	if (reflection) CollectFloatFamilyParams(*reflection, constantBufferName, parameters);
	if (!parameters.empty()) return parameters;
	const auto& storage = world.GetStorage().Get<AnimationMaterialContext>();
	const auto* material = storage.library ? storage.library->LoadMaterial(materialID) : nullptr;
	if (material) for (const auto& [name, value] : material->parameters) {
		std::visit([&](const auto& typed) {
			using T = std::decay_t<decltype(typed)>;
			if constexpr (std::is_same_v<T, float>) parameters.emplace_back(name, AnimationValueType::Float);
			else if constexpr (std::is_same_v<T, Vector2>) parameters.emplace_back(name, AnimationValueType::Vector2);
			else if constexpr (std::is_same_v<T, Vector3>) parameters.emplace_back(name, AnimationValueType::Vector3);
			else if constexpr (std::is_same_v<T, Vector4>) parameters.emplace_back(name, AnimationValueType::Vector4);
			else if constexpr (std::is_same_v<T, Color4>) parameters.emplace_back(name, AnimationValueType::Color4);
			else if constexpr (std::is_same_v<T, int32_t>) parameters.emplace_back(name, AnimationValueType::Int);
		}, value.value);
	}
	return parameters;
}
