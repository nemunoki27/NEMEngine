#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphSettingsImporter.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>

// c++
#include <algorithm>
#include <iostream>

namespace NEMTests {

	// 設定取り込みと失敗時の旧値維持を確認する
	bool TestShaderGraphSettingsImport(const ShaderGraphCompileFixture& fixture) {

		const auto& shaderRoot = fixture.GetShaderRoot();
		// アセット取り込みは保存先を維持し、失敗時に編集内容を変更しない
		{
			using namespace Engine;
			const AssetID graphID{1, 2}, materialID{1, 3};
			auto destination = CreateDefaultSurfaceShaderGraph("Destination");
			auto source = CreateDefaultSurfaceShaderGraph("Source");
			source.renderState.cullMode = D3D12_CULL_MODE_NONE;
			MaterialAsset material;
			nlohmann::json materialData;
			if (!JsonAdapter::TryLoad(shaderRoot / "Builtin/Mesh/MeshPBR/meshPBR.material.json", materialData)) {
				return false;
			}
			if (!FromJson(materialData, material)) {
				return false;
			}
			const auto resolver = [&](AssetID id, AssetType type, nlohmann::json& data) {
				if (id == AssetID{2, 1} && type == AssetType::Texture) {
					data = nlohmann::json::object();
					return true;
				}
				if (id == graphID && type == AssetType::ShaderGraph) {
					data = ToJson(source);
					return true;
				}
				if (id == materialID && type == AssetType::Material) {
					data = ToJson(material);
					return true;
				}
				if (id == BuiltinAssets::Materials::DefaultMesh && type == AssetType::Material) {
					data = materialData;
					return true;
				}
				std::string path;
				if (id == BuiltinAssets::Pipelines::DefaultMesh) {
					path = "meshPBR.pipeline.json";
				}
				if (id == BuiltinAssets::Pipelines::DefaultMeshMasked) {
					path = "meshPBRMasked.pipeline.json";
				}
				if (id == BuiltinAssets::Pipelines::DefaultMeshTransparent) {
					path = "meshPBRTransparent.pipeline.json";
				}
				if (path.empty() || type != AssetType::RenderPipeline) {
					return false;
				}
				return JsonAdapter::TryLoad(shaderRoot / "Builtin/Mesh/MeshPBR" / path, data);
			};
			ShaderGraphAsset imported;
			std::string error;
			if (!ShaderGraphSettingsImporter::Import(destination, graphID, AssetType::ShaderGraph, resolver, imported, error) ||
				imported.name != destination.name || imported.renderState.cullMode != D3D12_CULL_MODE_NONE ||
				imported.nodes.size() != source.nodes.size()) {
				std::cerr << "Graph import: " << error << '\n';
				return false;
			}
			if (!ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material, resolver, imported, error) ||
				!fixture.WriteGeneratedGraph(imported, "ImportedPBR")) {
				std::cerr << "PBR import: " << error << '\n';
				return false;
			}
			// 空のテクスチャも公開入力と接続を維持する
			const auto validateTextures = [](const ShaderGraphAsset& graph, AssetID expected) {
				for (const auto* name : {"baseColorTexture", "normalTexture", "metallicRoughnessTexture", "metallicTexture",
						 "roughnessTexture", "occlusionTexture", "emissiveTexture"}) {
					const auto parameter = std::find_if(graph.parameters.begin(), graph.parameters.end(),
						[name](const auto& value) { return value.name == name; });
					if (parameter == graph.parameters.end() || parameter->type != ShaderGraphValueType::Texture2D ||
						!parameter->exposed || !std::holds_alternative<AssetID>(parameter->defaultValue.value) ||
						std::get<AssetID>(parameter->defaultValue.value) != expected) {
						return false;
					}
					const auto node = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto& value) {
						return value.kind == ShaderGraphNodeKind::Parameter && value.parameterID == parameter->id;
					});
					if (node == graph.nodes.end()) {
						return false;
					}
					const auto connection = std::find_if(graph.links.begin(), graph.links.end(),
						[&](const auto& link) { return link.outputNode == node->id && link.inputSlot == 0; });
					if (connection == graph.links.end()) {
						return false;
					}
					const auto sample = std::find_if(graph.nodes.begin(), graph.nodes.end(),
						[&](const auto& value) { return value.id == connection->inputNode; });
					if (sample == graph.nodes.end() || sample->kind != ShaderGraphNodeKind::TextureSample ||
						!std::holds_alternative<Vector4>(sample->value.value)) {
						return false;
					}
					const auto fallback = std::get<Vector4>(sample->value.value);
					const bool normal = parameter->name == "normalTexture";
					if (fallback.x != (normal ? 0.5f : 1.0f) || fallback.y != (normal ? 0.5f : 1.0f) || fallback.z != 1.0f ||
						fallback.w != 1.0f) {
						return false;
					}
					if (std::none_of(graph.links.begin(), graph.links.end(),
							[&](const auto& link) { return link.inputNode == sample->id && link.inputSlot == 2; })) {
						return false;
					}
				}
				return true;
			};
			ShaderGraphAsset restoredImport;
			if (!validateTextures(imported, {}) || !FromJson(ToJson(imported), restoredImport) ||
				!validateTextures(restoredImport, {})) {
				return false;
			}
			const auto before = ToJson(imported);
			material.parameters.Set(MaterialParameterID::FromName("displacementScale"), "displacementScale",
				MaterialParameterSemantic::DisplacementScale, MaterialParameterValue{.value = 1.0f});
			if (ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material, resolver, imported, error) ||
				error.empty() || ToJson(imported) != before) {
				return false;
			}
			material.parameters.Set(MaterialParameterID::FromName("displacementScale"), "displacementScale",
				MaterialParameterSemantic::DisplacementScale, MaterialParameterValue{.value = 0.0f});
			for (const auto* name : {"baseColorTexture", "normalTexture", "metallicRoughnessTexture", "metallicTexture",
					 "roughnessTexture", "occlusionTexture", "emissiveTexture"}) {
				material.parameters.Set(MaterialParameterID::FromName(name), name, ResolveMaterialParameterSemantic(name),
					MaterialParameterValue{.value = AssetID{2, 1}});
			}
			if (!ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material, resolver, imported, error) ||
				!fixture.WriteGeneratedGraph(imported, "ImportedPBRTextures")) {
				std::cerr << "PBR texture import: " << error << '\n';
				return false;
			}
			if (!validateTextures(imported, AssetID{2, 1})) {
				return false;
			}
			source = imported;
			material = ShaderGraphArtifactCache::CreateMaterial(source, graphID);
			const auto sourceMetallic = std::find_if(source.parameters.begin(), source.parameters.end(),
				[](const auto& parameter) { return parameter.semantic == MaterialParameterSemantic::Metallic; });
			material.parameters.Set(MaterialParameterID::FromUUID(sourceMetallic->id), "metallic",
				MaterialParameterSemantic::Metallic, MaterialParameterValue{.value = 0.7f});
			if (!ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material, resolver, imported, error)) {
				std::cerr << "Graph material import: " << error << '\n';
				return false;
			}
			const auto metallic = std::find_if(imported.parameters.begin(), imported.parameters.end(),
				[](const auto& parameter) { return parameter.semantic == MaterialParameterSemantic::Metallic; });
			if (metallic == imported.parameters.end() || std::get<float>(metallic->defaultValue.value) != 0.7f) {
				return false;
			}
			const auto unchanged = ToJson(imported);
			material.passes.front().shaderOverride = AssetID{10, 20};
			if (ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material, resolver, imported, error) ||
				ToJson(imported) != unchanged) {
				return false;
			}
		}

		return true;
	}

} // NEMTests
