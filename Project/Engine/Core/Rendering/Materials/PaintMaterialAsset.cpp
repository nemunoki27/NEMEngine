#include "PaintMaterialAsset.h"

#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLookup.h>
#include <algorithm>

namespace {

	template<class T>
	void ReadParameter(const Engine::MaterialParameterSet& parameters, std::string_view name,
		Engine::MaterialParameterSemantic semantic, T& value) {

		const auto id = Engine::MaterialParameterID::FromName(name);
		const auto* parameter = semantic == Engine::MaterialParameterSemantic::None ? parameters.Find(id) :
			Engine::MaterialParameterLookup::Find(parameters, id, semantic, name);
		if (!parameter) return;
		if (const auto* typed = std::get_if<T>(&parameter->value)) value = *typed;
	}
}

bool Engine::IsPaintSettingParameter(std::string_view name) {

	return name == "paintChannels" || name == "paintHeight" || name == "paintHeightTexture" ||
		name == "paintNormalStrength" || name == "paintTiling";
}

Engine::PaintMaterialAsset Engine::MakePaintMaterialSample(const MaterialAsset& material) {

	PaintMaterialAsset paint;
	ReadParameter(material.parameters, MaterialParameterNames::BaseColor, MaterialParameterSemantic::BaseColor, paint.baseColor);
	ReadParameter(material.parameters, MaterialParameterNames::BaseColorTexture, MaterialParameterSemantic::BaseColorTexture, paint.baseColorTexture);
	ReadParameter(material.parameters, MaterialParameterNames::NormalTexture, MaterialParameterSemantic::NormalTexture, paint.normalTexture);
	ReadParameter(material.parameters, MaterialParameterNames::Roughness, MaterialParameterSemantic::Roughness, paint.roughness);
	ReadParameter(material.parameters, MaterialParameterNames::Metallic, MaterialParameterSemantic::Metallic, paint.metallic);
	ReadParameter(material.parameters, MaterialParameterNames::RoughnessTexture, MaterialParameterSemantic::RoughnessTexture, paint.roughnessTexture);
	ReadParameter(material.parameters, MaterialParameterNames::MetallicTexture, MaterialParameterSemantic::MetallicTexture, paint.metallicTexture);
	ReadParameter(material.parameters, MaterialParameterNames::AmbientOcclusion, MaterialParameterSemantic::AmbientOcclusion, paint.occlusion);
	ReadParameter(material.parameters, MaterialParameterNames::AmbientOcclusionTexture, MaterialParameterSemantic::AmbientOcclusionTexture, paint.occlusionTexture);
	ReadParameter(material.parameters, MaterialParameterNames::EmissiveColor, MaterialParameterSemantic::EmissiveColor, paint.emissiveColor);
	ReadParameter(material.parameters, MaterialParameterNames::EmissiveIntensity, MaterialParameterSemantic::EmissiveIntensity, paint.emissiveIntensity);
	ReadParameter(material.parameters, MaterialParameterNames::EmissiveTexture, MaterialParameterSemantic::EmissiveTexture, paint.emissiveTexture);
	ReadParameter(material.parameters, MaterialParameterNames::MetallicRoughnessTexture, MaterialParameterSemantic::MetallicRoughnessTexture, paint.metallicRoughnessTexture);
	ReadParameter(material.parameters, MaterialParameterNames::DisplacementTexture, MaterialParameterSemantic::DisplacementTexture, paint.heightTexture);
	ReadParameter(material.parameters, "paintChannels", MaterialParameterSemantic::None, paint.channels);
	ReadParameter(material.parameters, "paintHeight", MaterialParameterSemantic::None, paint.height);
	ReadParameter(material.parameters, "paintNormalStrength", MaterialParameterSemantic::None, paint.normalStrength);
	ReadParameter(material.parameters, "paintTiling", MaterialParameterSemantic::None, paint.tiling);
	ReadParameter(material.parameters, "paintHeightTexture", MaterialParameterSemantic::None, paint.heightTexture);
	return paint;
}

Engine::MaterialAsset Engine::ConvertLegacyPaintMaterial(std::string_view name, const PaintMaterialAsset& paint) {

	auto material = CreateDefaultMeshMaterialAsset(name);
	material.parameters.Set(MaterialParameterIDs::BaseColor, MaterialParameterNames::BaseColor,
		MaterialParameterSemantic::BaseColor, {.value = paint.baseColor});
	material.parameters.Set(MaterialParameterIDs::BaseColorTexture, MaterialParameterNames::BaseColorTexture,
		MaterialParameterSemantic::BaseColorTexture, {.value = paint.baseColorTexture});
	material.parameters.Set(MaterialParameterIDs::NormalTexture, MaterialParameterNames::NormalTexture,
		MaterialParameterSemantic::NormalTexture, {.value = paint.normalTexture});
	material.parameters.Set(MaterialParameterIDs::Roughness, MaterialParameterNames::Roughness,
		MaterialParameterSemantic::Roughness, {.value = paint.roughness});
	material.parameters.Set(MaterialParameterIDs::Metallic, MaterialParameterNames::Metallic,
		MaterialParameterSemantic::Metallic, {.value = paint.metallic});
	material.parameters.Set(MaterialParameterIDs::RoughnessTexture, MaterialParameterNames::RoughnessTexture,
		MaterialParameterSemantic::RoughnessTexture, {.value = paint.roughnessTexture});
	material.parameters.Set(MaterialParameterIDs::MetallicTexture, MaterialParameterNames::MetallicTexture,
		MaterialParameterSemantic::MetallicTexture, {.value = paint.metallicTexture});
	material.parameters.Set(MaterialParameterIDs::AmbientOcclusion, MaterialParameterNames::AmbientOcclusion,
		MaterialParameterSemantic::AmbientOcclusion, {.value = paint.occlusion});
	material.parameters.Set(MaterialParameterIDs::AmbientOcclusionTexture, MaterialParameterNames::AmbientOcclusionTexture,
		MaterialParameterSemantic::AmbientOcclusionTexture, {.value = paint.occlusionTexture});
	material.parameters.Set("paintChannels", {.value = paint.channels});
	material.parameters.Set("paintHeight", {.value = paint.height});
	material.parameters.Set("paintNormalStrength", {.value = paint.normalStrength});
	material.parameters.Set("paintTiling", {.value = paint.tiling});
	material.parameters.Set("paintHeightTexture", {.value = paint.heightTexture});
	return material;
}

nlohmann::json Engine::ToJson(const PaintMaterialAsset& material) {

	return {
		{"version", 1}, {"baseColor", {material.baseColor.r, material.baseColor.g, material.baseColor.b, material.baseColor.a}},
		{"normalStrength", material.normalStrength},
		{"roughness", material.roughness},
		{"metallic", material.metallic},
		{"occlusion", material.occlusion},
		{"height", material.height},
		{"tiling", material.tiling},
		{"channels", material.channels},
		{"baseColorTexture", ToAssetReferenceJson(material.baseColorTexture)},
		{"normalTexture", ToAssetReferenceJson(material.normalTexture)},
		{"roughnessTexture", ToAssetReferenceJson(material.roughnessTexture)},
		{"metallicTexture", ToAssetReferenceJson(material.metallicTexture)},
		{"occlusionTexture", ToAssetReferenceJson(material.occlusionTexture)},
		{"heightTexture", ToAssetReferenceJson(material.heightTexture)},
	};
}

bool Engine::LoadPaintMaterialAsset(const std::filesystem::path& path, PaintMaterialAsset& material) {

	const auto document = AssetFileUtility::LoadJsonFileNoThrow(path);
	if (!document.is_object()) return false;
	try {
		if (document.contains("passes")) {
			MaterialAsset surface;
			if (!FromJson(document, surface) || surface.domain != MaterialDomain::Surface) return false;
			material = MakePaintMaterialSample(surface);
			return true;
		}
		if (document.value("version", 0) != 1) return false;
		PaintMaterialAsset loaded;
		const auto color = document.at("baseColor").get<std::array<float, 4>>();
		loaded.baseColor = Color4(color[0], color[1], color[2], color[3]);
		loaded.normalStrength = std::clamp(document.value("normalStrength", loaded.normalStrength), 0.0f, 8.0f);
		loaded.roughness = std::clamp(document.value("roughness", loaded.roughness), 0.0f, 1.0f);
		loaded.metallic = std::clamp(document.value("metallic", loaded.metallic), 0.0f, 1.0f);
		loaded.occlusion = std::clamp(document.value("occlusion", loaded.occlusion), 0.0f, 1.0f);
		loaded.height = std::clamp(document.value("height", loaded.height), 0.0f, 1.0f);
		loaded.tiling = std::clamp(document.value("tiling", loaded.tiling), 0.001f, 10000.0f);
		loaded.channels = document.value("channels", 31u) & 31u;
		loaded.baseColorTexture = ParseAssetID(document, "baseColorTexture");
		loaded.normalTexture = ParseAssetID(document, "normalTexture");
		loaded.roughnessTexture = ParseAssetID(document, "roughnessTexture");
		loaded.metallicTexture = ParseAssetID(document, "metallicTexture");
		loaded.occlusionTexture = ParseAssetID(document, "occlusionTexture");
		loaded.heightTexture = ParseAssetID(document, "heightTexture");
		material = loaded;
		return true;
	} catch (const nlohmann::json::exception&) {
		return false;
	}
}
