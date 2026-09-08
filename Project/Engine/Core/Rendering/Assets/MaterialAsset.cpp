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
namespace {

	// JSONからMaterialParameterValueをパースする関数
	bool TryParseParameterValue(const nlohmann::json& data, Engine::MaterialParameterValue& outValue) {

		if (data.is_number_float()) {
			outValue.value = data.get<float>();
			return true;
		}
		if (data.is_boolean()) {
			outValue.value = data.get<bool>();
			return true;
		}
		if (data.is_number_integer()) {
			outValue.value = static_cast<int32_t>(data.get<int64_t>());
			return true;
		}
		if (data.is_number_unsigned()) {
			outValue.value = static_cast<uint32_t>(data.get<uint64_t>());
			return true;
		}
		if (data.is_string()) {
			const std::string text = data.get<std::string>();
			// 未設定のアセット参照も型を維持する
			if (text.empty()) {
				outValue.value = Engine::AssetID{};
				return true;
			}
			if (text.size() == 32) {
				outValue.value = Engine::FromString32Hex(text);
				return true;
			}
			return false;
		}
		if (data.is_array()) {
			if (data.size() == 2 && data[0].is_number() && data[1].is_number()) {

				outValue.value = Engine::Vector2(data[0].get<float>(), data[1].get<float>());
				return true;
			}
			if (data.size() == 3 && data[0].is_number() && data[1].is_number() && data[2].is_number()) {

				outValue.value = Engine::Vector3(data[0].get<float>(), data[1].get<float>(), data[2].get<float>());
				return true;
			}
			if (data.size() == 4 && data[0].is_number() && data[1].is_number() && data[2].is_number() && data[3].is_number()) {

				outValue.value = Engine::Vector4(data[0].get<float>(), data[1].get<float>(), data[2].get<float>(), data[3].get<float>());
				return true;
			}
		}
		if (data.is_object()) {
			if (data.contains("r") && data.contains("g") && data.contains("b") && data.contains("a")) {

				outValue.value = Engine::Color4(data.value("r", 0.0f), data.value("g", 0.0f), data.value("b", 0.0f), data.value("a", 1.0f));
				return true;
			}
		}
		return false;
	}

	// MaterialParameterValueをJSONへ変換する関数
	nlohmann::json SerializeParameterValue(const Engine::MaterialParameterValue& parameter) {

		return std::visit([](const auto& value) -> nlohmann::json {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float> ||
				std::is_same_v<ValueType, int32_t> ||
				std::is_same_v<ValueType, uint32_t> ||
				std::is_same_v<ValueType, bool>) {

				return value;
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {

				return nlohmann::json::array({ value.x, value.y });
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {

				return nlohmann::json::array({ value.x, value.y, value.z });
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {

				return nlohmann::json::array({ value.x, value.y, value.z, value.w });
			} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {

				return nlohmann::json{
					{ "r", value.r },
					{ "g", value.g },
					{ "b", value.b },
					{ "a", value.a },
				};
			} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {

				return Engine::ToAssetReferenceJson(value);
			} else {

				return nlohmann::json{};
			}
			}, parameter.value);
	}
}

bool Engine::ParseMaterialParameterValue(const nlohmann::json& data, MaterialParameterValue& outValue) {

	return TryParseParameterValue(data, outValue);
}

nlohmann::json Engine::SerializeMaterialParameterValue(const MaterialParameterValue& parameter) {

	return SerializeParameterValue(parameter);
}

void Engine::ReadMaterialInstance(const nlohmann::json& in,
	MaterialInstanceParameters& outOverrides) {

	outOverrides.clear();
	if (in.is_array()) {
		for (const nlohmann::json& record : in) {
			if (!record.is_object()) {
				continue;
			}

			const std::string name = record.value("name", "");
			const UUID parsedID =
				FromString16Hex(record.value("id", ""));
			if (name.empty()) {
				continue;
			}

			MaterialParameterValue value{};
			if (!record.contains("value") ||
				!ParseMaterialParameterValue(record["value"], value)) {

				continue;
			}
			const MaterialParameterSemantic semantic =
				EnumAdapter<MaterialParameterSemantic>::FromString(
					record.value("semantic", "None")).
				value_or(ResolveMaterialParameterSemantic(name));
			outOverrides.Set(
				parsedID ? MaterialParameterID::FromUUID(parsedID) :
				MaterialParameterID::FromName(name),
				name, semantic, value);
		}
		return;
	}
	if (!in.is_object()) {
		return;
	}

	// 旧object形式は読み込み時だけ受け、次回保存でID付きrecordへ移行する
	for (auto it = in.begin(); it != in.end(); ++it) {

		MaterialParameterValue value{};
		if (ParseMaterialParameterValue(it.value(), value)) {
			outOverrides[it.key()] = std::move(value);
		}
	}
}

nlohmann::json Engine::WriteMaterialInstance(
	const MaterialInstanceParameters& overrides) {

	nlohmann::json out = nlohmann::json::array();
	for (const MaterialParameterRecord& record : overrides.GetRecords()) {
		out.push_back({
			{ "id", ToString(UUID{ record.id.value }) },
			{ "name", record.namedValue.first },
			{ "semantic", EnumAdapter<MaterialParameterSemantic>::ToString(record.semantic) },
			{ "value", SerializeMaterialParameterValue(record.namedValue.second) },
			});
	}
	return out;
}

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
