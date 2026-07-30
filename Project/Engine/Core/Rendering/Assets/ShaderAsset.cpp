#include "ShaderAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	ShaderAsset classMethods
//============================================================================
bool Engine::FromJson(const nlohmann::json& data, ShaderAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = ShaderAsset{};
	outAsset.name = data.value("name", "UnnamedShader");

	if (data.contains("stages") && data["stages"].is_array()) {
		for (const auto& stageJson : data["stages"]) {

			if (!stageJson.is_object()) {
				continue;
			}

			ShaderStageEntry entry{};
			entry.stage = EnumAdapter<ShaderStage>::FromString(stageJson.value("stage", "None")).value_or(ShaderStage::None);
			entry.file = stageJson.value("file", "");
			entry.entry = stageJson.value("entry", "main");
			entry.profile = stageJson.value("profile", "");

			if (entry.stage == ShaderStage::None || entry.file.empty()) {
				continue;
			}
			outAsset.stages.emplace_back(std::move(entry));
		}
	}
	// 色として扱うパラメータ名を読む
	if (data.contains("colorParameters") && data["colorParameters"].is_array()) {
		for (const auto& nameJson : data["colorParameters"]) {
			if (nameJson.is_string()) {
				outAsset.colorParameters.emplace_back(nameJson.get<std::string>());
			}
		}
	}
	if (data.contains("parameters") &&
		data["parameters"].is_array()) {

		for (const nlohmann::json& item :
			data["parameters"]) {

			if (!item.is_object()) {
				continue;
			}
			ShaderParameterMetadata parameter{};
			parameter.shaderName =
				item.value("shaderName", "");
			parameter.displayName =
				item.value(
					"displayName",
					parameter.shaderName);
			const UUID id =
				FromString16Hex(
					item.value("id", ""));
			parameter.id =
				MaterialParameterID::FromUUID(id);
			parameter.semantic =
				EnumAdapter<MaterialParameterSemantic>::
				FromString(
					item.value("semantic", "None")).
				value_or(
					MaterialParameterSemantic::None);
			parameter.isColor =
				item.value("isColor", false);
			if (!parameter.shaderName.empty() &&
				parameter.id) {

				outAsset.parameters.emplace_back(
					std::move(parameter));
			}
		}
	}
	return true;
}

nlohmann::json Engine::ToJson(const ShaderAsset& asset) {

	nlohmann::json data = nlohmann::json::object();

	data["name"] = asset.name;
	data["stages"] = nlohmann::json::array();
	for (const auto& stage : asset.stages) {

		nlohmann::json item = nlohmann::json::object();
		item["stage"] = EnumAdapter<ShaderStage>::ToString(stage.stage);
		item["file"] = stage.file;
		item["entry"] = stage.entry;
		item["profile"] = stage.profile;
		data["stages"].push_back(item);
	}
	data["colorParameters"] = asset.colorParameters;
	data["parameters"] = nlohmann::json::array();
	for (const ShaderParameterMetadata& parameter :
		asset.parameters) {

		data["parameters"].push_back({
			{ "shaderName", parameter.shaderName },
			{ "displayName", parameter.displayName },
			{ "id", ToString(UUID{ parameter.id.value }) },
			{ "semantic", EnumAdapter<MaterialParameterSemantic>::ToString(parameter.semantic) },
			{ "isColor", parameter.isColor },
			});
	}
	return data;
}

const Engine::ShaderStageEntry* Engine::FindShaderStage(const ShaderAsset& asset, ShaderStage stage) {

	for (const auto& entry : asset.stages) {
		if (entry.stage == stage) {
			return &entry;
		}
	}
	return nullptr;
}

void Engine::ApplyShaderParameterMetadata(
	ShaderReflectionInfo& reflection,
	const ShaderAsset& asset) {

	auto applyVariable =
		[&](ShaderConstantBufferVariable& variable) {

		variable.isColor =
			std::find(
				asset.colorParameters.begin(),
				asset.colorParameters.end(),
				variable.name) !=
			asset.colorParameters.end();
		const auto metadata = std::find_if(
			asset.parameters.begin(),
			asset.parameters.end(),
			[&](const ShaderParameterMetadata& parameter) {
				return parameter.shaderName ==
					variable.name;
			});
		if (metadata == asset.parameters.end()) {
			return;
		}

		variable.parameterID = metadata->id;
		variable.semantic = metadata->semantic;
		variable.isColor |= metadata->isColor;
		if (!metadata->displayName.empty()) {
			variable.name = metadata->displayName;
		}
	};

	for (ShaderConstantBufferInfo& buffer :
		reflection.constantBuffers) {
		for (ShaderConstantBufferVariable& variable :
			buffer.variables) {

			applyVariable(variable);
		}
	}
	for (ShaderStructuredBufferInfo& buffer :
		reflection.structuredBuffers) {
		for (ShaderConstantBufferVariable& variable :
			buffer.variables) {

			applyVariable(variable);
		}
	}

	// リソース名はRoot Binding検索に使うため変更せずIDとSemanticだけ移す
	for (ShaderResourceBinding& resource :
		reflection.resources) {

		const auto metadata = std::find_if(
			asset.parameters.begin(),
			asset.parameters.end(),
			[&](const ShaderParameterMetadata& parameter) {
				return parameter.shaderName ==
					resource.name;
			});
		if (metadata == asset.parameters.end()) {
			continue;
		}
		resource.parameterID = metadata->id;
		resource.semantic = metadata->semantic;
	}
}
