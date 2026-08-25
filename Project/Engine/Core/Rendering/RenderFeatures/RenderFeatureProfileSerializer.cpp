#include "RenderFeatureProfileSerializer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

namespace {

	Engine::PipelineStaticSamplerSettings ParseSampler(
		const nlohmann::json& data) {

		Engine::PipelineStaticSamplerSettings settings{};
		if (!data.is_object()) {
			return settings;
		}
		const auto parse = [&]<typename T>(const char* name, T fallback) {

			return Engine::EnumAdapter<T>::FromString(data.value(
				name, std::string(Engine::EnumAdapter<T>::ToString(fallback)))).
				value_or(fallback);
		};
		settings.filter = parse("filter", settings.filter);
		settings.addressU = parse("addressU", settings.addressU);
		settings.addressV = parse("addressV", settings.addressV);
		settings.addressW = parse("addressW", settings.addressW);
		settings.borderColor = parse("borderColor", settings.borderColor);
		settings.comparisonFunc = parse(
			"comparisonFunc", settings.comparisonFunc);
		settings.maxAnisotropy = data.value(
			"maxAnisotropy", settings.maxAnisotropy);
		settings.mipLODBias = data.value("mipLODBias", settings.mipLODBias);
		settings.minLOD = data.value("minLOD", settings.minLOD);
		settings.maxLOD = data.value("maxLOD", settings.maxLOD);
		return settings;
	}

	nlohmann::json WriteSampler(
		const Engine::PipelineStaticSamplerSettings& settings) {

		return {
			{ "filter", Engine::EnumAdapter<D3D12_FILTER>::ToString(
				settings.filter) },
			{ "addressU", Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::
				ToString(settings.addressU) },
			{ "addressV", Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::
				ToString(settings.addressV) },
			{ "addressW", Engine::EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::
				ToString(settings.addressW) },
			{ "borderColor", Engine::EnumAdapter<D3D12_STATIC_BORDER_COLOR>::
				ToString(settings.borderColor) },
			{ "comparisonFunc", Engine::EnumAdapter<D3D12_COMPARISON_FUNC>::
				ToString(settings.comparisonFunc) },
			{ "maxAnisotropy", settings.maxAnisotropy },
			{ "mipLODBias", settings.mipLODBias },
			{ "minLOD", settings.minLOD },
			{ "maxLOD", settings.maxLOD },
		};
	}

	Engine::RenderFeatureOutputReference ParseOutputReference(
		const nlohmann::json& data) {

		Engine::RenderFeatureOutputReference reference{};
		if (!data.is_object()) {
			return reference;
		}
		reference.pass = Engine::FromString16Hex(
			data.value("pass", std::string{}));
		reference.output = data.value("output", "Color");
		return reference;
	}

	nlohmann::json WriteOutputReference(
		const Engine::RenderFeatureOutputReference& reference) {

		return {
			{ "pass", reference.pass ? Engine::ToString(reference.pass) : "" },
			{ "output", reference.output },
		};
	}

	Engine::RenderFeatureSelectionSettings ParseSelectionSettings(
		const nlohmann::json& data) {

		Engine::RenderFeatureSelectionSettings selection{};
		if (!data.is_object()) {
			return selection;
		}
		selection.mode = Engine::EnumAdapter<
			Engine::RenderFeatureSelectionMode>::FromString(
				data.value("mode", "Organization")).value_or(
					Engine::RenderFeatureSelectionMode::Organization);
		selection.anchor = Engine::EnumAdapter<
			Engine::RenderFeatureAnchor>::FromString(
				data.value("anchor", "AfterTransparent")).value_or(
					Engine::RenderFeatureAnchor::AfterTransparent);
		selection.renderingLayerMask = data.value(
			"renderingLayerMask", selection.renderingLayerMask) &
			Engine::kRenderingLayerMaskBits;
		selection.phaseMask = data.value(
			"phaseMask", selection.phaseMask) &
			Engine::kRenderFeatureSelectablePhaseMask;
		selection.rendererMask = data.value(
			"rendererMask", selection.rendererMask) &
			Engine::RenderFeatureRendererMask::All;
		selection.sortingLayer = data.value(
			"sortingLayer", selection.sortingLayer);
		selection.sortingOrder = data.value(
			"sortingOrder", selection.sortingOrder);
		selection.compositeMode = Engine::EnumAdapter<
			Engine::RenderFeatureCompositeMode>::FromString(
				data.value("compositeMode", "Auto")).value_or(
					Engine::RenderFeatureCompositeMode::Auto);
		return selection;
	}

	nlohmann::json WriteSelectionSettings(
		const Engine::RenderFeatureSelectionSettings& selection) {

		return {
			{ "mode", Engine::EnumAdapter<
				Engine::RenderFeatureSelectionMode>::ToString(selection.mode) },
			{ "anchor", Engine::EnumAdapter<
				Engine::RenderFeatureAnchor>::ToString(selection.anchor) },
			{ "renderingLayerMask", selection.renderingLayerMask &
				Engine::kRenderingLayerMaskBits },
			{ "phaseMask", selection.phaseMask &
				Engine::kRenderFeatureSelectablePhaseMask },
			{ "rendererMask", selection.rendererMask &
				Engine::RenderFeatureRendererMask::All },
			{ "sortingLayer", selection.sortingLayer },
			{ "sortingOrder", selection.sortingOrder },
			{ "compositeMode", Engine::EnumAdapter<
				Engine::RenderFeatureCompositeMode>::ToString(
					selection.compositeMode) },
		};
	}

	std::vector<Engine::RenderFeatureHierarchyItem> ParseHierarchyItems(
		const nlohmann::json& data) {

		std::vector<Engine::RenderFeatureHierarchyItem> items{};
		if (!data.is_array()) {
			return items;
		}
		for (const nlohmann::json& itemJson : data) {
			if (!itemJson.is_object()) {
				continue;
			}
			Engine::RenderFeatureHierarchyItem item{};
			item.type = itemJson.value("type", "Pass") == "Group" ?
				Engine::RenderFeatureHierarchyItemType::Group :
				Engine::RenderFeatureHierarchyItemType::Pass;
			item.id = Engine::FromString16Hex(
				itemJson.value("id", std::string{}));
			item.name = itemJson.value("name", item.name);
			item.enabled = itemJson.value("enabled", item.enabled);
			item.selection = ParseSelectionSettings(itemJson.value(
				"selection", nlohmann::json::object()));
			if (item.type == Engine::RenderFeatureHierarchyItemType::Group) {
				item.children = ParseHierarchyItems(
					itemJson.value("children", nlohmann::json::array()));
			}
			items.emplace_back(std::move(item));
		}
		return items;
	}

	nlohmann::json WriteHierarchyItems(
		const std::vector<Engine::RenderFeatureHierarchyItem>& items) {

		nlohmann::json data = nlohmann::json::array();
		for (const Engine::RenderFeatureHierarchyItem& item : items) {
			nlohmann::json itemJson{
				{ "type", item.type ==
					Engine::RenderFeatureHierarchyItemType::Group ?
					"Group" : "Pass" },
				{ "id", Engine::ToString(item.id) },
			};
			if (item.type == Engine::RenderFeatureHierarchyItemType::Group) {
				itemJson["name"] = item.name;
				itemJson["enabled"] = item.enabled;
				itemJson["selection"] = WriteSelectionSettings(item.selection);
				itemJson["children"] = WriteHierarchyItems(item.children);
			} else if (item.selection.mode !=
				Engine::RenderFeatureSelectionMode::Organization) {

				itemJson["selection"] = WriteSelectionSettings(item.selection);
			}
			data.push_back(std::move(itemJson));
		}
		return data;
	}

	void ParseColorPipeline(const nlohmann::json& data,
		Engine::ColorPipelineSettings& outSettings) {

		if (!data.is_object()) {
			return;
		}
		if (const auto exposure = data.find("exposure");
			exposure != data.end() && exposure->is_object()) {

			outSettings.exposure.mode =
				Engine::EnumAdapter<Engine::ExposureMode>::FromString(
					exposure->value("mode", "Manual")).
				value_or(Engine::ExposureMode::Manual);
			outSettings.exposure.manualEV100 = exposure->value(
				"manualEV100", outSettings.exposure.manualEV100);
			outSettings.exposure.compensation = exposure->value(
				"compensation", outSettings.exposure.compensation);
			outSettings.exposure.minEV100 = exposure->value(
				"minEV100", outSettings.exposure.minEV100);
			outSettings.exposure.maxEV100 = exposure->value(
				"maxEV100", outSettings.exposure.maxEV100);
			outSettings.exposure.histogramLowPercent = exposure->value(
				"histogramLowPercent",
				outSettings.exposure.histogramLowPercent);
			outSettings.exposure.histogramHighPercent = exposure->value(
				"histogramHighPercent",
				outSettings.exposure.histogramHighPercent);
			outSettings.exposure.speedUp = exposure->value(
				"speedUp", outSettings.exposure.speedUp);
			outSettings.exposure.speedDown = exposure->value(
				"speedDown", outSettings.exposure.speedDown);
			outSettings.exposure.usePreExposure = exposure->value(
				"usePreExposure", outSettings.exposure.usePreExposure);
		}
		if (const auto filmic = data.find("filmic");
			filmic != data.end() && filmic->is_object()) {

			outSettings.filmic.slope = filmic->value(
				"slope", outSettings.filmic.slope);
			outSettings.filmic.toe = filmic->value(
				"toe", outSettings.filmic.toe);
			outSettings.filmic.shoulder = filmic->value(
				"shoulder", outSettings.filmic.shoulder);
			outSettings.filmic.blackClip = filmic->value(
				"blackClip", outSettings.filmic.blackClip);
			outSettings.filmic.whiteClip = filmic->value(
				"whiteClip", outSettings.filmic.whiteClip);
		}
		if (const auto grading = data.find("colorGrading");
			grading != data.end() && grading->is_object()) {

			outSettings.colorGrading.colorFilter =
				Engine::JsonAdapter::GetColor4(
					*grading, "colorFilter", Engine::Color4::White());
			outSettings.colorGrading.temperature = grading->value(
				"temperature", outSettings.colorGrading.temperature);
			outSettings.colorGrading.tint = grading->value(
				"tint", outSettings.colorGrading.tint);
			outSettings.colorGrading.saturation =
				Engine::JsonAdapter::GetVector3(*grading, "saturation",
					Engine::Vector3::AnyInit(1.0f));
			outSettings.colorGrading.contrast =
				Engine::JsonAdapter::GetVector3(*grading, "contrast",
					Engine::Vector3::AnyInit(1.0f));
			outSettings.colorGrading.gamma =
				Engine::JsonAdapter::GetVector3(*grading, "gamma",
					Engine::Vector3::AnyInit(1.0f));
			outSettings.colorGrading.gain =
				Engine::JsonAdapter::GetVector3(*grading, "gain",
					Engine::Vector3::AnyInit(1.0f));
			outSettings.colorGrading.offset =
				Engine::JsonAdapter::GetVector3(*grading, "offset",
					Engine::Vector3::AnyInit(0.0f));
		}
	}

	nlohmann::json WriteColorPipeline(
		const Engine::ColorPipelineSettings& settings) {

		nlohmann::json grading = nlohmann::json::object();
		Engine::JsonAdapter::SetColor4(
			grading, "colorFilter", settings.colorGrading.colorFilter);
		grading["temperature"] = settings.colorGrading.temperature;
		grading["tint"] = settings.colorGrading.tint;
		Engine::JsonAdapter::SetVector3(
			grading, "saturation", settings.colorGrading.saturation);
		Engine::JsonAdapter::SetVector3(
			grading, "contrast", settings.colorGrading.contrast);
		Engine::JsonAdapter::SetVector3(
			grading, "gamma", settings.colorGrading.gamma);
		Engine::JsonAdapter::SetVector3(
			grading, "gain", settings.colorGrading.gain);
		Engine::JsonAdapter::SetVector3(
			grading, "offset", settings.colorGrading.offset);
		return {
			{ "exposure", {
				{ "mode", Engine::EnumAdapter<Engine::ExposureMode>::ToString(
					settings.exposure.mode) },
				{ "manualEV100", settings.exposure.manualEV100 },
				{ "compensation", settings.exposure.compensation },
				{ "minEV100", settings.exposure.minEV100 },
				{ "maxEV100", settings.exposure.maxEV100 },
				{ "histogramLowPercent", settings.exposure.histogramLowPercent },
				{ "histogramHighPercent", settings.exposure.histogramHighPercent },
				{ "speedUp", settings.exposure.speedUp },
				{ "speedDown", settings.exposure.speedDown },
				{ "usePreExposure", settings.exposure.usePreExposure },
			} },
			{ "filmic", {
				{ "slope", settings.filmic.slope },
				{ "toe", settings.filmic.toe },
				{ "shoulder", settings.filmic.shoulder },
				{ "blackClip", settings.filmic.blackClip },
				{ "whiteClip", settings.filmic.whiteClip },
			} },
			{ "colorGrading", std::move(grading) },
		};
	}
}

bool Engine::FromJson(const nlohmann::json& data,
	RenderFeatureProfileAsset& outProfile) {

	if (!data.is_object()) {
		return false;
	}
	outProfile = RenderFeatureProfileSerializer::FromJson(data);
	return true;
}

nlohmann::json Engine::ToJson(
	const RenderFeatureProfileAsset& profile) {

	return RenderFeatureProfileSerializer::ToJson(profile);
}

//============================================================================
//	RenderFeatureProfileSerializer classMethods
//============================================================================
bool Engine::RenderFeatureProfileSerializer::Load(
	const std::filesystem::path& path,
	RenderFeatureProfileAsset& outProfile) {

	if (!std::filesystem::exists(path)) {
		return false;
	}
	const nlohmann::json data = JsonAdapter::Load(path, false);
	if (!data.is_object()) {
		return false;
	}
	outProfile = FromJson(data);
	return true;
}

bool Engine::RenderFeatureProfileSerializer::Save(
	const std::filesystem::path& path,
	const RenderFeatureProfileAsset& profile) {

	const std::filesystem::path directory = path.parent_path();
	if (!directory.empty()) {
		std::filesystem::create_directories(directory);
	}
	JsonAdapter::Save(path, ToJson(profile));
	return true;
}

Engine::RenderFeatureProfileAsset
Engine::RenderFeatureProfileSerializer::FromJson(
	const nlohmann::json& data) {

	RenderFeatureProfileAsset profile{};
	profile.version = data.value("version", 1u);
	profile.name = data.value("name", profile.name);
	if (const auto colorPipeline = data.find("colorPipeline");
		colorPipeline != data.end()) {

		ParseColorPipeline(*colorPipeline, profile.colorPipeline);
	}
	if (!data.contains("passes") || !data["passes"].is_array()) {
		return profile;
	}

	for (const nlohmann::json& passJson : data["passes"]) {

		if (!passJson.is_object()) {
			continue;
		}
		RenderFeaturePassSettings pass{};
		pass.id = FromString16Hex(passJson.value("id", std::string{}));
		if (!pass.id) {
			continue;
		}
		pass.name = passJson.value("name", pass.name);
		pass.enabled = passJson.value("enabled", pass.enabled);
		pass.gameView = passJson.value("gameView", pass.gameView);
		pass.sceneView = passJson.value("sceneView", pass.sceneView);
		pass.type = EnumAdapter<RenderFeaturePassType>::FromString(
			passJson.value("type", "Compute")).value_or(
				RenderFeaturePassType::Compute);
		pass.anchor = EnumAdapter<RenderFeatureAnchor>::FromString(
			passJson.value("anchor", "AfterTransparent")).value_or(
				RenderFeatureAnchor::AfterTransparent);
		pass.material = ParseAssetID(passJson, "material");
		pass.materialPass = EnumAdapter<MaterialPassKind>::FromString(
			passJson.value("materialPass", "PostProcess")).value_or(
				MaterialPassKind::PostProcess);
		const nlohmann::json sourceJson = passJson.value(
			"source", nlohmann::json::object());
		pass.sourceKind = EnumAdapter<RenderFeatureSourceKind>::FromString(
			sourceJson.value("kind", "PreviousPass")).value_or(
				RenderFeatureSourceKind::PreviousPass);
		pass.source = ParseOutputReference(sourceJson);
		pass.sceneColorOutput = passJson.value(
			"sceneColorOutput", false);
		pass.rayGenerationIndex = passJson.value(
			"rayGenerationIndex", 0u);
		pass.adaptiveResolution = passJson.value(
			"adaptiveResolution", pass.adaptiveResolution);
		pass.gpuBudgetMs = passJson.value("gpuBudgetMs", pass.gpuBudgetMs);
		pass.minResolutionScale = passJson.value(
			"minResolutionScale", pass.minResolutionScale);
		pass.maxResolutionScale = passJson.value(
			"maxResolutionScale", pass.maxResolutionScale);
		pass.resolutionStep = passJson.value(
			"resolutionStep", pass.resolutionStep);
		pass.adjustmentIntervalFrames = passJson.value(
			"adjustmentIntervalFrames", pass.adjustmentIntervalFrames);

		for (const nlohmann::json& output : passJson.value(
			"outputs", nlohmann::json::array())) {

			if (!output.is_object()) {
				continue;
			}
			RenderFeatureOutputSettings settings{};
			settings.name = output.value("name", settings.name);
			settings.shaderResource = output.value(
				"shaderResource", settings.shaderResource);
			settings.format = EnumAdapter<RenderFeatureTextureFormat>::
				FromString(output.value("format", "Inherit")).value_or(
					RenderFeatureTextureFormat::Inherit);
			settings.widthScale = output.value("widthScale", 1.0f);
			settings.heightScale = output.value("heightScale", 1.0f);
			settings.history = output.value("history", settings.history);
			settings.historyShaderResource = output.value(
				"historyShaderResource", settings.historyShaderResource);
			if (output.contains("clearColor")) {
				settings.clearColor = JsonAdapter::GetColor4(
					output, "clearColor", Color4::Black());
			}
			pass.outputs.emplace_back(std::move(settings));
		}

		const nlohmann::json parameters = passJson.value(
			"parameters", nlohmann::json::object());
		for (auto it = parameters.begin(); it != parameters.end(); ++it) {

			MaterialParameterValue value{};
			if (ParseMaterialParameterValue(it.value(), value)) {
				pass.parameterOverrides[it.key()] = std::move(value);
			}
		}
		const nlohmann::json textures = passJson.value(
			"textures", nlohmann::json::object());
		for (auto it = textures.begin(); it != textures.end(); ++it) {

			if (it.value().is_string()) {
				pass.textureOverrides[it.key()] =
					TryParseAssetGUID32Hex(it.value().get<std::string>()).
					value_or(AssetID{});
			}
		}
		const nlohmann::json sceneInputs = passJson.value(
			"sceneInputs", nlohmann::json::object());
		for (auto it = sceneInputs.begin(); it != sceneInputs.end(); ++it) {

			if (it.value().is_string()) {
				pass.sceneInputs[it.key()] = it.value().get<std::string>();
			}
		}
		const nlohmann::json passInputs = passJson.value(
			"passInputs", nlohmann::json::object());
		for (auto it = passInputs.begin(); it != passInputs.end(); ++it) {

			const RenderFeatureOutputReference reference =
				ParseOutputReference(it.value());
			if (reference.pass) {
				pass.passInputs[it.key()] = reference;
			}
		}
		const nlohmann::json samplers = passJson.value(
			"samplers", nlohmann::json::object());
		for (auto it = samplers.begin(); it != samplers.end(); ++it) {

			pass.samplerOverrides[it.key()] = ParseSampler(it.value());
		}
		profile.passes.emplace_back(std::move(pass));
	}
	profile.hierarchy = ParseHierarchyItems(data.value(
		"hierarchy", nlohmann::json::array()));
	SynchronizeRenderFeaturePassOrder(profile);
	return profile;
}

nlohmann::json Engine::RenderFeatureProfileSerializer::ToJson(
	const RenderFeatureProfileAsset& profile) {

	RenderFeatureProfileAsset normalized = profile;
	SynchronizeRenderFeaturePassOrder(normalized);
	nlohmann::json data{
		{ "version", 3u },
		{ "name", normalized.name },
		{ "colorPipeline", WriteColorPipeline(normalized.colorPipeline) },
		{ "passes", nlohmann::json::array() },
		{ "hierarchy", WriteHierarchyItems(normalized.hierarchy) },
	};
	for (const RenderFeaturePassSettings& pass : normalized.passes) {

		nlohmann::json sourceJson = WriteOutputReference(pass.source);
		sourceJson["kind"] = EnumAdapter<RenderFeatureSourceKind>::ToString(
			pass.sourceKind);
		nlohmann::json passJson{
			{ "id", ToString(pass.id) },
			{ "name", pass.name },
			{ "enabled", pass.enabled },
			{ "gameView", pass.gameView },
			{ "sceneView", pass.sceneView },
			{ "type", EnumAdapter<RenderFeaturePassType>::ToString(pass.type) },
			{ "anchor", EnumAdapter<RenderFeatureAnchor>::ToString(pass.anchor) },
			{ "material", ToAssetReferenceJson(pass.material) },
			{ "materialPass", EnumAdapter<MaterialPassKind>::ToString(
				pass.materialPass) },
			{ "source", std::move(sourceJson) },
			{ "sceneColorOutput", pass.sceneColorOutput },
			{ "rayGenerationIndex", pass.rayGenerationIndex },
			{ "adaptiveResolution", pass.adaptiveResolution },
			{ "gpuBudgetMs", pass.gpuBudgetMs },
			{ "minResolutionScale", pass.minResolutionScale },
			{ "maxResolutionScale", pass.maxResolutionScale },
			{ "resolutionStep", pass.resolutionStep },
			{ "adjustmentIntervalFrames", pass.adjustmentIntervalFrames },
			{ "outputs", nlohmann::json::array() },
			{ "parameters", nlohmann::json::object() },
			{ "textures", nlohmann::json::object() },
			{ "sceneInputs", pass.sceneInputs },
			{ "passInputs", nlohmann::json::object() },
			{ "samplers", nlohmann::json::object() },
		};
		for (const RenderFeatureOutputSettings& output : pass.outputs) {

			nlohmann::json outputJson{
				{ "name", output.name },
				{ "shaderResource", output.shaderResource },
				{ "format", EnumAdapter<RenderFeatureTextureFormat>::
					ToString(output.format) },
				{ "widthScale", output.widthScale },
				{ "heightScale", output.heightScale },
				{ "history", output.history },
				{ "historyShaderResource", output.historyShaderResource },
			};
			if (output.clearColor) {
				JsonAdapter::SetColor4(
					outputJson, "clearColor", *output.clearColor);
			}
			passJson["outputs"].push_back(std::move(outputJson));
		}
		for (const auto& [name, value] : pass.parameterOverrides) {
			passJson["parameters"][name] =
				SerializeMaterialParameterValue(value);
		}
		for (const auto& [name, texture] : pass.textureOverrides) {
			passJson["textures"][name] = ToAssetReferenceJson(texture);
		}
		for (const auto& [name, input] : pass.passInputs) {
			passJson["passInputs"][name] = WriteOutputReference(input);
		}
		for (const auto& [name, sampler] : pass.samplerOverrides) {
			passJson["samplers"][name] = WriteSampler(sampler);
		}
		data["passes"].push_back(std::move(passJson));
	}
	return data;
}
