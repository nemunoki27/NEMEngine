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
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <array>
#include <iostream>

namespace NEMTests {

	// 設定取り込みと失敗時の旧値維持を確認する
	bool TestShaderGraphSettingsImport(const ShaderGraphCompileFixture& fixture) {

		// 旧形式の両面描画と各カリング値を保存後も維持する
		for (const auto cullMode : {D3D12_CULL_MODE_NONE, D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_BACK}) {
			auto graph = Engine::CreateDefaultSurfaceShaderGraph("CullMode");
			graph.renderState.cullMode = cullMode;
			const auto saved = Engine::ToJson(graph);
			Engine::ShaderGraphAsset restored;
			if (saved["renderState"].contains("twoSided") || !Engine::FromJson(saved, restored) ||
				restored.renderState.cullMode != cullMode) {
				return false;
			}
			for (const bool twoSided : {false, true}) {
				auto legacy = saved;
				legacy["renderState"]["twoSided"] = twoSided;
				const auto expected = twoSided ? D3D12_CULL_MODE_NONE : cullMode;
				if (!Engine::FromJson(legacy, restored) || restored.renderState.cullMode != expected) {
					return false;
				}
				const auto converted = Engine::ToJson(restored);
				if (converted["renderState"].contains("twoSided") || !Engine::FromJson(converted, restored) ||
					restored.renderState.cullMode != expected) {
					return false;
				}
			}
		}
		const auto renderingRoot = fixture.GetShaderRoot().parent_path() / "Rendering";
		// アセット取り込みは保存先を維持し、失敗時に編集内容を変更しない
		{
			using namespace Engine;
			const AssetID graphID{1, 2}, materialID{1, 3};
			auto destination = CreateDefaultSurfaceShaderGraph("Destination");
			auto source = CreateDefaultSurfaceShaderGraph("Source");
			source.renderState.cullMode = D3D12_CULL_MODE_NONE;
			MaterialAsset material;
			nlohmann::json materialData;
			if (!JsonAdapter::TryLoad(renderingRoot / "Builtin/Mesh/MeshPBR/meshPBR.material.json", materialData)) {
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
				return JsonAdapter::TryLoad(renderingRoot / "Builtin/Mesh/MeshPBR" / path, data);
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

	bool TestShaderGraphRuntimeInputs(const ShaderGraphCompileFixture& fixture) {

		using namespace Engine;
		struct Input {

			ShaderGraphNodeKind kind;
			uint32_t outputSlot;
			ShaderGraphValueType type;
			const char* name;
		};
		const std::array inputs{
			Input{ShaderGraphNodeKind::Camera, 0, ShaderGraphValueType::Float3, "cameraPosition"},
			Input{ShaderGraphNodeKind::Camera, 1, ShaderGraphValueType::Float3, "cameraDirection"},
			Input{ShaderGraphNodeKind::Object, 0, ShaderGraphValueType::Float3, "objectPosition"},
			Input{ShaderGraphNodeKind::Object, 4, ShaderGraphValueType::Float3, "objectSize"},
			Input{ShaderGraphNodeKind::Transform, 0, ShaderGraphValueType::Float3, "transformed"},
			Input{ShaderGraphNodeKind::TransformationMatrix, 0, ShaderGraphValueType::Matrix4, "objectMatrix"},
			Input{ShaderGraphNodeKind::Screen, 0, ShaderGraphValueType::Float, "screenWidth"},
			Input{ShaderGraphNodeKind::WorldNormal, 0, ShaderGraphValueType::Float3, "normal"},
			Input{ShaderGraphNodeKind::BitangentVector, 0, ShaderGraphValueType::Float3, "bitangent"},
			Input{ShaderGraphNodeKind::IsFrontFace, 0, ShaderGraphValueType::Boolean, "front"},
			Input{ShaderGraphNodeKind::TextureSample, 0, ShaderGraphValueType::Float4, "sample"},
		};
		for (const auto target : {ShaderGraphTarget::Mesh, ShaderGraphTarget::Primitive3D, ShaderGraphTarget::Sprite,
			ShaderGraphTarget::Text, ShaderGraphTarget::Primitive2D, ShaderGraphTarget::Particle, ShaderGraphTarget::Trail}) {
			auto graph = CreateDefaultSurfaceShaderGraph("RuntimeInputs", target);
			const auto addNode = [&](ShaderGraphNodeKind kind) {
				const Engine::UUID id = Engine::UUID::New();
				graph.nodes.emplace_back(ShaderGraphNode{.id = id, .kind = kind});
				return id;
			};
			const auto connect = [&](Engine::UUID source, uint32_t sourceSlot, Engine::UUID destination, uint32_t destinationSlot) {
				graph.links.emplace_back(ShaderGraphLink{
					.id = Engine::UUID::New(), .outputNode = source, .outputSlot = sourceSlot,
					.inputNode = destination, .inputSlot = destinationSlot,
				});
			};
			ShaderGraphNode function{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::CustomFunction};
			function.functionName = "CheckRuntimeInputs";
			function.functionBody =
				"void CheckRuntimeInputs(float3 cameraPosition, float3 cameraDirection, float3 objectPosition, "
				"float3 objectSize, float3 transformed, float4x4 objectMatrix, float screenWidth, float3 normal, "
				"float3 bitangent, uint front, float4 sample, out float4 color) { "
				"float3 sum = cameraPosition + cameraDirection + objectPosition + objectSize + transformed + "
				"objectMatrix[0].xyz + screenWidth.xxx + normal + bitangent + float(front).xxx; "
				"color = float4(sum, 1) * sample; }";
			for (uint32_t slot = 0; slot < inputs.size(); ++slot) {
				const auto& input = inputs[slot];
				function.inputPorts.emplace_back(ShaderGraphPort{
					.id = Engine::UUID::New(), .name = input.name, .type = input.type,
				});
				const Engine::UUID nodeID = addNode(input.kind);
				connect(nodeID, input.outputSlot, function.id, slot);
				if (input.kind == ShaderGraphNodeKind::Transform) {
					connect(addNode(ShaderGraphNodeKind::WorldPosition), 0, nodeID, 0);
				}
				if (input.kind == ShaderGraphNodeKind::TextureSample) {
					const Engine::UUID parameterID = Engine::UUID::New();
					graph.parameters.emplace_back(ShaderGraphParameter{
						.id = parameterID, .name = "Map", .type = ShaderGraphValueType::Texture2D,
						.defaultValue = {AssetID{}},
					});
					const Engine::UUID parameterNode = addNode(ShaderGraphNodeKind::Parameter);
					graph.nodes.back().parameterID = parameterID;
					connect(parameterNode, 0, nodeID, 0);
				}
			}
			function.outputPorts.emplace_back(ShaderGraphPort{
				.id = Engine::UUID::New(), .name = "Color", .type = ShaderGraphValueType::Float4,
			});
			std::erase_if(graph.links, [&](const ShaderGraphLink& link) {
				return link.inputNode == graph.outputNode && link.inputSlot == 0;
			});
			connect(function.id, 0, graph.outputNode, 0);
			graph.nodes.emplace_back(std::move(function));
			const auto generated = ShaderGraphCompiler::Compile(graph, "surface.hlsli");
			if (!generated.Succeeded()) return false;
			for (const auto* source : {&generated.opaquePixelHLSL, &generated.transparentPixelHLSL}) {
				if (source->find("graphInput.cameraPosition =") == std::string::npos ||
					source->find("graphInput.worldToObject =") == std::string::npos ||
					source->find("graphInput.isFrontFace =") == std::string::npos) return false;
			}
			if (!generated.particleGPUPixelHLSL.empty() &&
				generated.particleGPUPixelHLSL.find("graphInput.cameraPosition =") == std::string::npos) return false;
			const std::string name = std::string("RuntimeInputs_") + EnumAdapter<ShaderGraphTarget>::ToString(target);
			if (!fixture.WriteGeneratedGraph(graph, name) || !fixture.CompileRasterShaders(name, target)) return false;
		}
		return true;
	}

} // NEMTests
