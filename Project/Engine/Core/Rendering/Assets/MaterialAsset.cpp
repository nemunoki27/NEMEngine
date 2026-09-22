#include "MaterialAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <type_traits>

//============================================================================
//	MaterialAsset classMethods
//============================================================================
bool Engine::FromJson(const nlohmann::json& data, MaterialAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = MaterialAsset{};
	outAsset.name = data.value("name", "UnnamedMaterial");
	outAsset.domain = EnumAdapter<MaterialDomain>::FromString(data.value("domain", "Surface")).value_or(MaterialDomain::Surface);
	outAsset.usage = EnumAdapter<MaterialUsage>::FromString(data.value("usage", "Generic")).value_or(MaterialUsage::Generic);
	outAsset.shaderGraph = ParseAssetID(data, "shaderGraph");
	if (data.contains("renderState") && data["renderState"].is_object()) {
		const nlohmann::json& renderState = data["renderState"];
		outAsset.renderState.overridesRenderer =
			renderState.value("overridesRenderer", false);
		outAsset.renderState.phase = RenderPhaseFromString(
			renderState.value("phase", "Opaque"), RenderPhase::Opaque);
		outAsset.renderState.surfaceMode =
			EnumAdapter<MaterialSurfaceMode>::FromString(
				renderState.value("surfaceMode",
					outAsset.renderState.phase == RenderPhase::Transparent ?
					"Transparent" : "Opaque")).
			value_or(MaterialSurfaceMode::Opaque);
		outAsset.renderState.blendMode =
			EnumAdapter<BlendMode>::FromString(
				renderState.value("blendMode", "Normal")).
			value_or(BlendMode::Normal);
		outAsset.renderState.castShadows =
			renderState.value("castShadows", true);
		outAsset.renderState.receiveShadows =
			renderState.value("receiveShadows", true);
	}
	if (data.contains("passes") && data["passes"].is_array()) {
		for (const auto& passJson : data["passes"]) {

			if (!passJson.is_object()) {
				continue;
			}

			MaterialPassBinding binding{};
			const auto passKind = EnumAdapter<MaterialPassKind>::FromString(passJson.value("passKind", ""));
			if (!passKind || *passKind == MaterialPassKind::Invalid) {
				continue;
			}
			binding.passKind = *passKind;
			binding.pipeline = ParseAssetID(passJson, "pipeline");
			binding.shaderOverride = ParseAssetID(passJson, "shaderOverride");
			binding.preferredVariant = EnumAdapter<PipelineVariantKind>::FromString(passJson.value("preferredVariant",
				"GraphicsVertex")).value_or(PipelineVariantKind::GraphicsVertex);

			if (!binding.pipeline) {
				continue;
			}
			outAsset.passes.emplace_back(std::move(binding));
		}
	}

	if (data.contains("parameters")) {
		ReadMaterialInstance(data["parameters"], outAsset.parameters);
	}
	return true;
}

nlohmann::json Engine::ToJson(const MaterialAsset& asset) {

	nlohmann::json data = nlohmann::json::object();

	data["name"] = asset.name;
	data["domain"] = EnumAdapter<MaterialDomain>::ToString(asset.domain);
	data["usage"] = EnumAdapter<MaterialUsage>::ToString(asset.usage);
	if (asset.shaderGraph) {
		data["shaderGraph"] = ToAssetReferenceJson(asset.shaderGraph);
	}
	if (asset.renderState.overridesRenderer) {
		data["renderState"] = {
			{ "overridesRenderer", true },
			{ "surfaceMode", EnumAdapter<MaterialSurfaceMode>::ToString(
				asset.renderState.surfaceMode) },
			{ "phase", std::string(ToString(asset.renderState.phase)) },
			{ "blendMode", EnumAdapter<BlendMode>::ToString(
				asset.renderState.blendMode) },
			{ "castShadows", asset.renderState.castShadows },
			{ "receiveShadows", asset.renderState.receiveShadows },
		};
	}
	data["passes"] = nlohmann::json::array();
	for (const auto& pass : asset.passes) {
		nlohmann::json item = nlohmann::json::object();
		item["passKind"] = EnumAdapter<MaterialPassKind>::ToString(pass.passKind);
		item["pipeline"] = ToAssetReferenceJson(pass.pipeline);
		if (pass.shaderOverride) {
			item["shaderOverride"] = ToAssetReferenceJson(pass.shaderOverride);
		}
		item["preferredVariant"] = EnumAdapter<PipelineVariantKind>::ToString(pass.preferredVariant);
		data["passes"].push_back(item);
	}
	data["parameters"] = WriteMaterialInstance(asset.parameters);
	return data;
}

Engine::MaterialAsset Engine::CreateDefaultMeshMaterialAsset(std::string_view name) {

	MaterialAsset material{};
	material.name = name.empty() ? "NewMaterial" : std::string(name);
	material.domain = MaterialDomain::Surface;
	material.usage = MaterialUsage::Mesh;
	material.passes = {
		MaterialPassBinding{
			.passKind = MaterialPassKind::ZPrepass,
			.pipeline = BuiltinAssets::Pipelines::DefaultMeshZPrepass,
			.preferredVariant = PipelineVariantKind::GraphicsMesh,
		},
		MaterialPassBinding{
			.passKind = MaterialPassKind::EditorPicking,
			.pipeline = BuiltinAssets::Pipelines::DefaultMeshEditorPicking,
			.preferredVariant = PipelineVariantKind::GraphicsVertex,
		},
		MaterialPassBinding{
			.passKind = MaterialPassKind::Draw,
			.pipeline = BuiltinAssets::Pipelines::DefaultMesh,
			.preferredVariant = PipelineVariantKind::GraphicsMesh,
		},
		MaterialPassBinding{
			.passKind = MaterialPassKind::Masked,
			.pipeline = BuiltinAssets::Pipelines::DefaultMeshMasked,
			.preferredVariant = PipelineVariantKind::GraphicsMesh,
		},
		MaterialPassBinding{
			.passKind = MaterialPassKind::Transparent,
			.pipeline = BuiltinAssets::Pipelines::DefaultMeshTransparent,
			.preferredVariant = PipelineVariantKind::GraphicsMesh,
		},
	};

	material.parameters.Set(MaterialParameterIDs::BaseColor,
		MaterialParameterNames::BaseColor,
		MaterialParameterSemantic::BaseColor,
		MaterialParameterValue{ .value = Color4::White() });
	material.parameters.Set(MaterialParameterIDs::EmissiveColor,
		MaterialParameterNames::EmissiveColor,
		MaterialParameterSemantic::EmissiveColor,
		MaterialParameterValue{ .value = Color4{} });
	material.parameters.Set(MaterialParameterIDs::Metallic,
		MaterialParameterNames::Metallic,
		MaterialParameterSemantic::Metallic,
		MaterialParameterValue{ .value = 0.0f });
	material.parameters.Set(MaterialParameterIDs::Roughness,
		MaterialParameterNames::Roughness,
		MaterialParameterSemantic::Roughness,
		MaterialParameterValue{ .value = 0.5f });
	material.parameters.Set(MaterialParameterIDs::EmissiveIntensity,
		MaterialParameterNames::EmissiveIntensity,
		MaterialParameterSemantic::EmissiveIntensity,
		MaterialParameterValue{ .value = 0.0f });
	material.parameters.Set(MaterialParameterIDs::DisplacementMidpoint,
		MaterialParameterNames::DisplacementMidpoint,
		MaterialParameterSemantic::DisplacementMidpoint,
		MaterialParameterValue{ .value = 0.5f });
	material.parameters.Set(MaterialParameterIDs::DisplacementScale,
		MaterialParameterNames::DisplacementScale,
		MaterialParameterSemantic::DisplacementScale,
		MaterialParameterValue{ .value = 0.0f });

	return material;
}

Engine::MaterialPassBinding* Engine::FindPass(MaterialAsset& asset, MaterialPassKind passKind) {

	for (auto& pass : asset.passes) {
		if (pass.passKind == passKind) {
			return &pass;
		}
	}
	return nullptr;
}

const Engine::MaterialPassBinding* Engine::FindPass(const MaterialAsset& asset, MaterialPassKind passKind) {

	for (const auto& pass : asset.passes) {
		if (pass.passKind == passKind) {
			return &pass;
		}
	}
	return nullptr;
}
