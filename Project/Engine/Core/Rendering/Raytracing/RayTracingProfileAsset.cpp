#include "RayTracingProfileAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

//============================================================================
//	RayTracingProfile functions
//============================================================================
bool Engine::FromJson(const nlohmann::json& data,
	RayTracingProfileAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}
	outAsset = RayTracingProfileAsset{};
	outAsset.name = data.value("name", outAsset.name);
	outAsset.version = data.value("version", 1u);
	for (const nlohmann::json& effectJson :
		data.value("effects", nlohmann::json::array())) {

		if (!effectJson.is_object()) {
			continue;
		}
		RayTracingEffectSettings effect{};
		effect.id = FromString16Hex(effectJson.value("id", ""));
		if (!effect.id) {
			continue;
		}
		effect.name = effectJson.value("name", effect.name);
		effect.enabled = effectJson.value("enabled", true);
		effect.gameView = effectJson.value("gameView", true);
		effect.sceneView = effectJson.value("sceneView", true);
		effect.executionPoint =
			EnumAdapter<RayTracingExecutionPoint>::FromString(
				effectJson.value("executionPoint", "AfterLighting"))
			.value_or(RayTracingExecutionPoint::AfterLighting);
		effect.material = ParseAssetID(effectJson, "material");
		effect.rayGenerationIndex = effectJson.value(
			"rayGenerationIndex", 0u);

		for (const nlohmann::json& inputJson :
			effectJson.value("inputs", nlohmann::json::array())) {

			if (!inputJson.is_object()) {
				continue;
			}
			RayTracingInputBinding input{};
			input.shaderResource = inputJson.value("shaderResource", "");
			input.source = EnumAdapter<RayTracingTextureSource>::FromString(
				inputJson.value("source", "SceneColor"))
				.value_or(RayTracingTextureSource::SceneColor);
			if (!input.shaderResource.empty()) {
				effect.inputs.emplace_back(std::move(input));
			}
		}

		for (const nlohmann::json& textureJson :
			effectJson.value("textures", nlohmann::json::array())) {

			if (!textureJson.is_object()) {
				continue;
			}
			RayTracingTextureBinding texture{};
			texture.shaderResource = textureJson.value(
				"shaderResource", "");
			texture.texture = ParseAssetID(textureJson, "texture");
			if (!texture.shaderResource.empty()) {
				effect.textures.emplace_back(std::move(texture));
			}
		}

		if (const auto parameters = effectJson.find("parameters");
			parameters != effectJson.end() && parameters->is_object()) {

			for (auto it = parameters->begin(); it != parameters->end(); ++it) {
				MaterialParameterValue value{};
				if (ParseMaterialParameterValue(it.value(), value)) {
					effect.parameterOverrides[it.key()] = std::move(value);
				}
			}
		}
		outAsset.effects.emplace_back(std::move(effect));
	}
	return true;
}

nlohmann::json Engine::ToJson(const RayTracingProfileAsset& asset) {

	nlohmann::json data = {
		{ "name", asset.name },
		{ "version", 1u },
		{ "effects", nlohmann::json::array() },
	};
	for (const RayTracingEffectSettings& effect : asset.effects) {

		nlohmann::json effectJson = {
			{ "id", ToString(effect.id) },
			{ "name", effect.name },
			{ "enabled", effect.enabled },
			{ "gameView", effect.gameView },
			{ "sceneView", effect.sceneView },
			{ "executionPoint", EnumAdapter<RayTracingExecutionPoint>::ToString(
				effect.executionPoint) },
			{ "material", ToAssetReferenceJson(effect.material) },
			{ "rayGenerationIndex", effect.rayGenerationIndex },
			{ "inputs", nlohmann::json::array() },
			{ "textures", nlohmann::json::array() },
			{ "parameters", nlohmann::json::object() },
		};
		for (const RayTracingInputBinding& input : effect.inputs) {
			effectJson["inputs"].push_back({
				{ "shaderResource", input.shaderResource },
				{ "source", EnumAdapter<RayTracingTextureSource>::ToString(
					input.source) },
			});
		}
		for (const RayTracingTextureBinding& texture : effect.textures) {
			effectJson["textures"].push_back({
				{ "shaderResource", texture.shaderResource },
				{ "texture", ToAssetReferenceJson(texture.texture) },
			});
		}
		for (const auto& [name, value] : effect.parameterOverrides) {
			effectJson["parameters"][name] =
				SerializeMaterialParameterValue(value);
		}
		data["effects"].push_back(std::move(effectJson));
	}
	return data;
}
