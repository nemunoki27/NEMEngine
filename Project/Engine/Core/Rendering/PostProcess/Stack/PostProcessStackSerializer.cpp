#include "PostProcessStackSerializer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <filesystem>

namespace {

	// JSONから静的サンプラー設定を読む
	Engine::PipelineStaticSamplerSettings ParseSamplerSettings(const nlohmann::json& data) {

		Engine::PipelineStaticSamplerSettings settings{};
		if (!data.is_object()) {
			return settings;
		}

		settings.filter = Engine::EnumAdapter<D3D12_FILTER>::FromString(data.value("filter",
			std::string(Engine::EnumAdapter<D3D12_FILTER>::ToString(settings.filter)))).value_or(settings.filter);
		settings.addressU = Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(data.value("addressU",
			std::string(Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(settings.addressU)))).value_or(settings.addressU);
		settings.addressV = Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(data.value("addressV",
			std::string(Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(settings.addressV)))).value_or(settings.addressV);
		settings.addressW = Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(data.value("addressW",
			std::string(Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(settings.addressW)))).value_or(settings.addressW);
		settings.borderColor = Engine::EnumAdapter<D3D12_STATIC_BORDER_COLOR>::FromString(data.value("borderColor",
			std::string(Engine::EnumAdapter<D3D12_STATIC_BORDER_COLOR>::ToString(settings.borderColor)))).value_or(settings.borderColor);
		settings.comparisonFunc = Engine::EnumAdapter<D3D12_COMPARISON_FUNC>::FromString(data.value("comparisonFunc",
			std::string(Engine::EnumAdapter<D3D12_COMPARISON_FUNC>::ToString(settings.comparisonFunc)))).value_or(settings.comparisonFunc);
		settings.maxAnisotropy = data.value("maxAnisotropy", settings.maxAnisotropy);
		settings.mipLODBias = data.value("mipLODBias", settings.mipLODBias);
		settings.minLOD = data.value("minLOD", settings.minLOD);
		settings.maxLOD = data.value("maxLOD", settings.maxLOD);
		return settings;
	}

	// 静的サンプラー設定をJSONへ書く
	nlohmann::json WriteSamplerSettings(const Engine::PipelineStaticSamplerSettings& settings) {

		nlohmann::json data = nlohmann::json::object();
		data["filter"] = Engine::EnumAdapter<D3D12_FILTER>::ToString(settings.filter);
		data["addressU"] = Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(settings.addressU);
		data["addressV"] = Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(settings.addressV);
		data["addressW"] = Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(settings.addressW);
		data["borderColor"] = Engine::EnumAdapter<D3D12_STATIC_BORDER_COLOR>::ToString(settings.borderColor);
		data["comparisonFunc"] = Engine::EnumAdapter<D3D12_COMPARISON_FUNC>::ToString(settings.comparisonFunc);
		data["maxAnisotropy"] = settings.maxAnisotropy;
		data["mipLODBias"] = settings.mipLODBias;
		data["minLOD"] = settings.minLOD;
		data["maxLOD"] = settings.maxLOD;
		return data;
	}
}

//============================================================================
//	PostProcessStackSerializer classMethods
//============================================================================
bool Engine::PostProcessStackSerializer::Load(const std::filesystem::path& path, PostProcessStackSettings& outSettings) {

	if (!std::filesystem::exists(path)) {
		return false;
	}

	const nlohmann::json data = JsonAdapter::Load(path, false);
	if (!data.is_object()) {
		return false;
	}

	outSettings = FromJson(data);
	return true;
}

bool Engine::PostProcessStackSerializer::Save(const std::filesystem::path& path, const PostProcessStackSettings& settings) {

	const std::filesystem::path dir = path.parent_path();
	if (!dir.empty() && !std::filesystem::exists(dir)) {
		std::filesystem::create_directories(dir);
	}

	JsonAdapter::Save(path, ToJson(settings));
	return true;
}

Engine::PostProcessStackSettings Engine::PostProcessStackSerializer::FromJson(const nlohmann::json& data) {

	PostProcessStackSettings settings{};
	settings.version = data.value("version", 1);

	if (data.contains("colorPipeline") && data["colorPipeline"].is_object()) {

		const nlohmann::json& colorPipeline = data["colorPipeline"];
		if (colorPipeline.contains("exposure") && colorPipeline["exposure"].is_object()) {

			const nlohmann::json& exposure = colorPipeline["exposure"];
			settings.colorPipeline.exposure.mode =
				EnumAdapter<ExposureMode>::FromString(
					exposure.value("mode", "Manual"))
				.value_or(ExposureMode::Manual);
			settings.colorPipeline.exposure.manualEV100 =
				exposure.value("manualEV100", 0.0f);
			settings.colorPipeline.exposure.compensation =
				exposure.value("compensation", 0.0f);
			settings.colorPipeline.exposure.minEV100 =
				exposure.value("minEV100", -10.0f);
			settings.colorPipeline.exposure.maxEV100 =
				exposure.value("maxEV100", 20.0f);
			settings.colorPipeline.exposure.histogramLowPercent =
				exposure.value("histogramLowPercent", 0.8f);
			settings.colorPipeline.exposure.histogramHighPercent =
				exposure.value("histogramHighPercent", 0.95f);
			settings.colorPipeline.exposure.speedUp =
				exposure.value("speedUp", 3.0f);
			settings.colorPipeline.exposure.speedDown =
				exposure.value("speedDown", 1.0f);
			settings.colorPipeline.exposure.usePreExposure =
				exposure.value("usePreExposure", true);
		}

		if (colorPipeline.contains("filmic") && colorPipeline["filmic"].is_object()) {

			const nlohmann::json& filmic = colorPipeline["filmic"];
			settings.colorPipeline.filmic.slope = filmic.value("slope", 1.0f);
			settings.colorPipeline.filmic.toe = filmic.value("toe", 0.0f);
			settings.colorPipeline.filmic.shoulder = filmic.value("shoulder", 0.0f);
			settings.colorPipeline.filmic.blackClip = filmic.value("blackClip", 0.0f);
			settings.colorPipeline.filmic.whiteClip = filmic.value("whiteClip", 0.0f);
		}

		if (colorPipeline.contains("colorGrading") &&
			colorPipeline["colorGrading"].is_object()) {

			const nlohmann::json& grading = colorPipeline["colorGrading"];
			settings.colorPipeline.colorGrading.colorFilter =
				JsonAdapter::GetColor4(grading, "colorFilter", Color4::White());
			settings.colorPipeline.colorGrading.temperature =
				grading.value("temperature", 6500.0f);
			settings.colorPipeline.colorGrading.tint = grading.value("tint", 0.0f);
			settings.colorPipeline.colorGrading.saturation =
				JsonAdapter::GetVector3(grading, "saturation", Vector3::AnyInit(1.0f));
			settings.colorPipeline.colorGrading.contrast =
				JsonAdapter::GetVector3(grading, "contrast", Vector3::AnyInit(1.0f));
			settings.colorPipeline.colorGrading.gamma =
				JsonAdapter::GetVector3(grading, "gamma", Vector3::AnyInit(1.0f));
			settings.colorPipeline.colorGrading.gain =
				JsonAdapter::GetVector3(grading, "gain", Vector3::AnyInit(1.0f));
			settings.colorPipeline.colorGrading.offset =
				JsonAdapter::GetVector3(grading, "offset", Vector3::AnyInit(0.0f));
		}
	}

	if (!data.contains("passes") || !data["passes"].is_array()) {
		return settings;
	}

	for (const auto& passJson : data["passes"]) {

		if (!passJson.is_object()) {
			continue;
		}

		PostProcessStackPassSettings pass{};
		const std::string idStr = passJson.value("id", "");
		pass.id = idStr.size() == 16 ? FromString16Hex(idStr) : UUID{};
		if (!pass.id) {
			continue;
		}
		pass.name = passJson.value("name", "Pass");
		pass.enabled = passJson.value("enabled", true);

		const std::string guidStr = passJson.value("materialGuid", "");
		pass.materialGuid = guidStr.size() == 32 ? FromString32Hex(guidStr) : AssetID{};
		const auto passKind = EnumAdapter<MaterialPassKind>::FromString(passJson.value("passKind", ""));
		if (!passKind || *passKind == MaterialPassKind::Invalid) {
			continue;
		}
		pass.passKind = *passKind;

		const auto anchor = EnumAdapter<PostProcessAnchor>::FromString(passJson.value("anchor", ""));
		if (!anchor) {
			continue;
		}
		pass.anchor = *anchor;
		pass.sourcePass =
			FromString16Hex(
				passJson.value("sourcePass", ""));
		pass.graphOutput =
			passJson.value("graphOutput", false);
		pass.targetMask =
			passJson.value("targetMask", 0u) &
			kRenderingLayerMaskBits;

		if (passJson.contains("parameters") && passJson["parameters"].is_object()) {
			for (auto it = passJson["parameters"].begin(); it != passJson["parameters"].end(); ++it) {
				MaterialParameterValue value{};
				if (Engine::ParseMaterialParameterValue(it.value(), value)) {
					pass.parameterOverrides[it.key()] = std::move(value);
				}
			}
		}

		if (passJson.contains("textures") && passJson["textures"].is_object()) {
			for (auto it = passJson["textures"].begin(); it != passJson["textures"].end(); ++it) {
				if (!it.value().is_object()) {
					continue;
				}
				const std::string texGuidStr = it.value().value("textureGuid", "");
				if (texGuidStr.size() == 32) {
					pass.textureGuids[it.key()] = FromString32Hex(texGuidStr);
				}
			}
		}

		if (passJson.contains("renderTargetInputs") && passJson["renderTargetInputs"].is_object()) {
			for (auto it = passJson["renderTargetInputs"].begin(); it != passJson["renderTargetInputs"].end(); ++it) {
				if (it.value().is_string()) {
					pass.renderTargetInputs[it.key()] = it.value().get<std::string>();
				}
			}
		}

		if (passJson.contains("passInputs") &&
			passJson["passInputs"].is_object()) {

			for (auto it = passJson["passInputs"].begin();
				it != passJson["passInputs"].end(); ++it) {

				if (!it.value().is_string()) {
					continue;
				}
				const UUID source =
					FromString16Hex(
						it.value().get<std::string>());
				if (source) {
					pass.passInputs[it.key()] = source;
				}
			}
		}

		if (passJson.contains("samplers") && passJson["samplers"].is_object()) {
			for (auto it = passJson["samplers"].begin(); it != passJson["samplers"].end(); ++it) {
				pass.samplerOverrides[it.key()] = ParseSamplerSettings(it.value());
			}
		}

		settings.passes.emplace_back(std::move(pass));
	}

	return settings;
}

nlohmann::json Engine::PostProcessStackSerializer::ToJson(const PostProcessStackSettings& stackSettings) {

	nlohmann::json data = nlohmann::json::object();
	data["version"] = 3;

	const ColorPipelineSettings& colorPipeline = stackSettings.colorPipeline;
	data["colorPipeline"] = nlohmann::json::object();
	data["colorPipeline"]["exposure"] = {
		{ "mode", EnumAdapter<ExposureMode>::ToString(colorPipeline.exposure.mode) },
		{ "manualEV100", colorPipeline.exposure.manualEV100 },
		{ "compensation", colorPipeline.exposure.compensation },
		{ "minEV100", colorPipeline.exposure.minEV100 },
		{ "maxEV100", colorPipeline.exposure.maxEV100 },
		{ "histogramLowPercent", colorPipeline.exposure.histogramLowPercent },
		{ "histogramHighPercent", colorPipeline.exposure.histogramHighPercent },
		{ "speedUp", colorPipeline.exposure.speedUp },
		{ "speedDown", colorPipeline.exposure.speedDown },
		{ "usePreExposure", colorPipeline.exposure.usePreExposure },
	};
	data["colorPipeline"]["filmic"] = {
		{ "slope", colorPipeline.filmic.slope },
		{ "toe", colorPipeline.filmic.toe },
		{ "shoulder", colorPipeline.filmic.shoulder },
		{ "blackClip", colorPipeline.filmic.blackClip },
		{ "whiteClip", colorPipeline.filmic.whiteClip },
	};
	nlohmann::json grading = nlohmann::json::object();
	JsonAdapter::SetColor4(grading, "colorFilter", colorPipeline.colorGrading.colorFilter);
	grading["temperature"] = colorPipeline.colorGrading.temperature;
	grading["tint"] = colorPipeline.colorGrading.tint;
	JsonAdapter::SetVector3(grading, "saturation", colorPipeline.colorGrading.saturation);
	JsonAdapter::SetVector3(grading, "contrast", colorPipeline.colorGrading.contrast);
	JsonAdapter::SetVector3(grading, "gamma", colorPipeline.colorGrading.gamma);
	JsonAdapter::SetVector3(grading, "gain", colorPipeline.colorGrading.gain);
	JsonAdapter::SetVector3(grading, "offset", colorPipeline.colorGrading.offset);
	data["colorPipeline"]["colorGrading"] = std::move(grading);

	data["passes"] = nlohmann::json::array();
	for (const auto& pass : stackSettings.passes) {

		nlohmann::json passJson = nlohmann::json::object();
		passJson["id"] = ToString(pass.id);
		passJson["name"] = pass.name;
		passJson["enabled"] = pass.enabled;
		passJson["materialGuid"] = ToAssetReferenceJson(pass.materialGuid);
		passJson["passKind"] = EnumAdapter<MaterialPassKind>::ToString(pass.passKind);
		passJson["anchor"] = EnumAdapter<PostProcessAnchor>::ToString(pass.anchor);
		passJson["sourcePass"] =
			pass.sourcePass ? ToString(pass.sourcePass) : "";
		passJson["graphOutput"] = pass.graphOutput;
		passJson["targetMask"] =
			pass.targetMask &
			kRenderingLayerMaskBits;

		passJson["parameters"] = nlohmann::json::object();
		for (const auto& [name, value] : pass.parameterOverrides) {
			passJson["parameters"][name] = Engine::SerializeMaterialParameterValue(value);
		}

		passJson["textures"] = nlohmann::json::object();
		for (const auto& [name, guid] : pass.textureGuids) {
			nlohmann::json texJson = nlohmann::json::object();
			texJson["textureGuid"] = ToAssetReferenceJson(guid);
			passJson["textures"][name] = texJson;
		}

		passJson["renderTargetInputs"] = nlohmann::json::object();
		for (const auto& [name, source] : pass.renderTargetInputs) {
			passJson["renderTargetInputs"][name] = source;
		}

		passJson["passInputs"] = nlohmann::json::object();
		for (const auto& [name, source] : pass.passInputs) {
			passJson["passInputs"][name] =
				ToString(source);
		}

		passJson["samplers"] = nlohmann::json::object();
		for (const auto& [name, settings] : pass.samplerOverrides) {
			passJson["samplers"][name] = WriteSamplerSettings(settings);
		}

		data["passes"].push_back(std::move(passJson));
	}

	return data;
}
