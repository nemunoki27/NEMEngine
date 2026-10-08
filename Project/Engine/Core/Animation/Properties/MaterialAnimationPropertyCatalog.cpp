#include "MaterialAnimationPropertyCatalog.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>

namespace {

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
		const std::filesystem::path path = context.assetDatabase->ResolveFullPath(materialID);
		if (path.empty()) {
			return nullptr;
		}
		nlohmann::json data = Engine::JsonAdapter::Load(path, false);
		Engine::MaterialAsset material{};
		if (!Engine::FromJson(data, material)) {
			return nullptr;
		}
		return context.renderPipeline->FindMaterialDrawReflection(material);
	}
}

std::vector<std::pair<std::string, Engine::AnimationValueType>> Engine::CollectMaterialAnimationParameters(
	const AnimationPropertyQueryContext& context, AssetID materialID, const std::string& constantBufferName) {

	// Materialが参照するDrawパスの型情報を取得する
	const ShaderReflectionInfo* reflection = GetMaterialDrawReflection(context, materialID);
	if (!reflection) {
		return {};
	}

	// 未使用の変数とTexture参照を除く
	std::vector<std::pair<std::string, AnimationValueType>> parameters;
	CollectFloatFamilyParams(*reflection, constantBufferName, parameters);
	return parameters;
}
