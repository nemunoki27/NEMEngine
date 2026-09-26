#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderReflectionParser.h>

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/BlendState.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphSettingsImporter.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestShaderGraphCompile() {

		TestDirectory directory("ShaderGraph");
		const auto& generatedRoot = directory.GetPath();
		const auto shaderRoot = Engine::RuntimePaths::GetEngineAssetsRoot() / "Shaders";
		std::error_code ec{};
		std::filesystem::create_directories(
			generatedRoot, ec);
		if (ec) {
			return false;
		}
		auto writeGeneratedGraph =
			[&](const Engine::ShaderGraphAsset& sourceGraph,
				std::string_view name) {

			const std::filesystem::path graphRoot =
				generatedRoot / std::string(name);
			std::filesystem::create_directories(
				graphRoot, ec);
			if (ec) {
				return false;
			}
			const std::filesystem::path surfacePath =
				graphRoot / "surface.hlsli";
			const std::filesystem::path opaquePath =
				graphRoot / "opaque.PS.hlsl";
			const std::filesystem::path transparentPath =
				graphRoot / "transparent.PS.hlsl";
			const std::filesystem::path vertexPath =
				graphRoot / "vertex.VS.hlsl";
			const std::filesystem::path meshPath =
				graphRoot / "mesh.MS.hlsl";
			const std::filesystem::path rayTracingPath =
				graphRoot / "rayTracing.RT.hlsl";
			const Engine::ShaderGraphCompileOutput generated =
				Engine::ShaderGraphCompiler::Compile(
					sourceGraph, "surface.hlsli");
			if (!generated.Succeeded()) {
				return false;
			}
			auto write = [](const std::filesystem::path& path,
				std::string_view source) {

				std::ofstream stream(
					path, std::ios::binary |
					std::ios::trunc);
				stream.write(
					source.data(),
					static_cast<std::streamsize>(
						source.size()));
				return stream.good();
			};
			if (!write(surfacePath, generated.surfaceHLSL) ||
				!write(opaquePath, generated.opaquePixelHLSL) ||
				!write(
					transparentPath,
					generated.transparentPixelHLSL)) {

				return false;
			}
			if ((!generated.vertexHLSL.empty() &&
				!write(vertexPath, generated.vertexHLSL)) ||
				(!generated.meshHLSL.empty() &&
					!write(meshPath, generated.meshHLSL)) ||
				(!generated.rayTracingHLSL.empty() &&
					!write(rayTracingPath, generated.rayTracingHLSL))) {

				return false;
			}
			if ((!generated.depthPixelHLSL.empty() &&
				!write(graphRoot / "depth.PS.hlsl", generated.depthPixelHLSL)) ||
				(!generated.pickingPixelHLSL.empty() &&
					!write(graphRoot / "picking.PS.hlsl", generated.pickingPixelHLSL))) {

				return false;
			}
			return true;
		};

		// アセット取り込みは保存先を維持し、失敗時に編集内容を変更しない
		{
			using namespace Engine;
			const AssetID graphID{ 1, 2 }, materialID{ 1, 3 };
			auto destination = CreateDefaultSurfaceShaderGraph("Destination");
			auto source = CreateDefaultSurfaceShaderGraph("Source");
			source.renderState.cullMode = D3D12_CULL_MODE_NONE;
			MaterialAsset material;
			nlohmann::json materialData;
			if (!JsonAdapter::TryLoad(shaderRoot / "Builtin/Mesh/MeshPBR/meshPBR.material.json", materialData)) return false;
			if (!FromJson(materialData, material)) return false;
			const auto resolver = [&](AssetID id, AssetType type, nlohmann::json& data) {
				if (id == AssetID{ 2, 1 } && type == AssetType::Texture) { data = nlohmann::json::object(); return true; }
				if (id == graphID && type == AssetType::ShaderGraph) { data = ToJson(source); return true; }
				if (id == materialID && type == AssetType::Material) { data = ToJson(material); return true; }
				if (id == BuiltinAssets::Materials::DefaultMesh && type == AssetType::Material) { data = materialData; return true; }
				std::string path;
				if (id == BuiltinAssets::Pipelines::DefaultMesh) path = "meshPBR.pipeline.json";
				if (id == BuiltinAssets::Pipelines::DefaultMeshMasked) path = "meshPBRMasked.pipeline.json";
				if (id == BuiltinAssets::Pipelines::DefaultMeshTransparent) path = "meshPBRTransparent.pipeline.json";
				if (path.empty() || type != AssetType::RenderPipeline) return false;
				return JsonAdapter::TryLoad(shaderRoot / "Builtin/Mesh/MeshPBR" / path, data);
			};
			ShaderGraphAsset imported;
			std::string error;
			if (!ShaderGraphSettingsImporter::Import(destination, graphID, AssetType::ShaderGraph,
				resolver, imported, error) || imported.name != destination.name ||
				imported.renderState.cullMode != D3D12_CULL_MODE_NONE || imported.nodes.size() != source.nodes.size()) {
				std::cerr << "Graph import: " << error << '\n';
				return false;
			}
			if (!ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material,
				resolver, imported, error) || !writeGeneratedGraph(imported, "ImportedPBR")) {
				std::cerr << "PBR import: " << error << '\n';
				return false;
			}
			// 空のテクスチャも公開入力と接続を維持する
			const auto validateTextures = [](const ShaderGraphAsset& graph, AssetID expected) {
				for (const auto* name : { "baseColorTexture", "normalTexture", "metallicRoughnessTexture",
					"metallicTexture", "roughnessTexture", "occlusionTexture", "emissiveTexture" }) {
					const auto parameter = std::find_if(graph.parameters.begin(), graph.parameters.end(),
						[name](const auto& value) { return value.name == name; });
					if (parameter == graph.parameters.end() || parameter->type != ShaderGraphValueType::Texture2D ||
						!parameter->exposed || !std::holds_alternative<AssetID>(parameter->defaultValue.value) ||
						std::get<AssetID>(parameter->defaultValue.value) != expected) return false;
					const auto node = std::find_if(graph.nodes.begin(), graph.nodes.end(),
						[&](const auto& value) { return value.kind == ShaderGraphNodeKind::Parameter && value.parameterID == parameter->id; });
					if (node == graph.nodes.end()) return false;
					const auto connection = std::find_if(graph.links.begin(), graph.links.end(),
						[&](const auto& link) { return link.outputNode == node->id && link.inputSlot == 0; });
					if (connection == graph.links.end()) return false;
					const auto sample = std::find_if(graph.nodes.begin(), graph.nodes.end(),
						[&](const auto& value) { return value.id == connection->inputNode; });
					if (sample == graph.nodes.end() || sample->kind != ShaderGraphNodeKind::TextureSample ||
						!std::holds_alternative<Vector4>(sample->value.value)) return false;
					const auto fallback = std::get<Vector4>(sample->value.value);
					const bool normal = parameter->name == "normalTexture";
					if (fallback.x != (normal ? 0.5f : 1.0f) || fallback.y != (normal ? 0.5f : 1.0f) ||
						fallback.z != 1.0f || fallback.w != 1.0f) return false;
					if (std::none_of(graph.links.begin(), graph.links.end(),
						[&](const auto& link) { return link.inputNode == sample->id && link.inputSlot == 2; })) return false;
				}
				return true;
			};
			ShaderGraphAsset restoredImport;
			if (!validateTextures(imported, {}) || !FromJson(ToJson(imported), restoredImport) ||
				!validateTextures(restoredImport, {})) return false;
			const auto before = ToJson(imported);
			material.parameters.Set(MaterialParameterID::FromName("displacementScale"), "displacementScale",
				MaterialParameterSemantic::DisplacementScale, MaterialParameterValue{ .value = 1.0f });
			if (ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material,
				resolver, imported, error) || error.empty() || ToJson(imported) != before) return false;
			material.parameters.Set(MaterialParameterID::FromName("displacementScale"), "displacementScale",
				MaterialParameterSemantic::DisplacementScale, MaterialParameterValue{ .value = 0.0f });
			for (const auto* name : { "baseColorTexture", "normalTexture", "metallicRoughnessTexture",
				"metallicTexture", "roughnessTexture", "occlusionTexture", "emissiveTexture" }) {
				material.parameters.Set(MaterialParameterID::FromName(name), name,
					ResolveMaterialParameterSemantic(name), MaterialParameterValue{ .value = AssetID{ 2, 1 } });
			}
			if (!ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material,
				resolver, imported, error) || !writeGeneratedGraph(imported, "ImportedPBRTextures")) {
				std::cerr << "PBR texture import: " << error << '\n';
				return false;
			}
			if (!validateTextures(imported, AssetID{ 2, 1 })) return false;
			source = imported;
			material = ShaderGraphArtifactCache::CreateMaterial(source, graphID);
			const auto sourceMetallic = std::find_if(source.parameters.begin(), source.parameters.end(),
				[](const auto& parameter) { return parameter.semantic == MaterialParameterSemantic::Metallic; });
			material.parameters.Set(MaterialParameterID::FromUUID(sourceMetallic->id), "metallic",
				MaterialParameterSemantic::Metallic, MaterialParameterValue{ .value = 0.7f });
			if (!ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material,
				resolver, imported, error)) {
				std::cerr << "Graph material import: " << error << '\n';
				return false;
			}
			const auto metallic = std::find_if(imported.parameters.begin(), imported.parameters.end(),
				[](const auto& parameter) { return parameter.semantic == MaterialParameterSemantic::Metallic; });
			if (metallic == imported.parameters.end() || std::get<float>(metallic->defaultValue.value) != 0.7f) return false;
			const auto unchanged = ToJson(imported);
			material.passes.front().shaderOverride = AssetID{ 10, 20 };
			if (ShaderGraphSettingsImporter::Import(destination, materialID, AssetType::Material,
				resolver, imported, error) || ToJson(imported) != unchanged) return false;
		}

		Engine::ShaderGraphAsset graph =
			Engine::CreateDefaultSurfaceShaderGraph("NEMTest");
		const Engine::ShaderGraphCompileOutput output =
			Engine::ShaderGraphCompiler::Compile(
				graph, "NEMTest.surface.hlsli");
		if (!output.Succeeded() ||
			output.parameters.size() != graph.parameters.size() ||
			!graph.parameters.empty() ||
			graph.nodes.size() != 8 ||
			output.surfaceHLSL.find("EvaluateShaderGraphSurface") ==
				std::string::npos ||
			output.surfaceHLSL.find("ShaderGraphTimeConstants") ==
				std::string::npos ||
			output.opaquePixelHLSL.find("EncodeGBuffer") ==
				std::string::npos ||
			output.transparentPixelHLSL.find("EvaluateMeshSurfaceLighting") ==
				std::string::npos ||
			output.rayTracingHLSL.find("ReflectionAnyHit") ==
				std::string::npos ||
			output.rayTracingHLSL.find("ReflectionClosestHit") ==
				std::string::npos) {

			return false;
		}

		Engine::ShaderGraphAsset groupedGraph = graph;
		const Engine::UUID groupID = Engine::UUID::New();
		groupedGraph.groups.emplace_back(
			Engine::ShaderGraphGroup{
				.id = groupID,
				.name = "NoiseA",
				.position = Engine::Vector2(32.0f, 64.0f),
				.size = Engine::Vector2(320.0f, 180.0f),
			});
		groupedGraph.nodes.front().groupID = groupID;
		Engine::ShaderGraphAsset restoredGroup{};
		if (!Engine::FromJson(
			Engine::ToJson(groupedGraph), restoredGroup) ||
			restoredGroup.groups.size() != 1 ||
			restoredGroup.nodes.front().groupID != groupID) {

			return false;
		}
		if (!writeGeneratedGraph(graph, "Mesh")) {
			return false;
		}

		// Runtime KeywordはMaterial値、Static Keywordは保存時の定数へ変換する
		Engine::ShaderGraphAsset keywordGraph =
			Engine::CreateDefaultSurfaceShaderGraph("NEMKeywordTest");
		std::erase_if(keywordGraph.links,
			[&](const Engine::ShaderGraphLink& link) {
				return link.inputNode == keywordGraph.outputNode &&
					link.inputSlot == 2;
			});
		const Engine::UUID keywordID = Engine::UUID::New();
		const Engine::UUID keywordNodeID = Engine::UUID::New();
		keywordGraph.keywords.emplace_back(Engine::ShaderGraphKeyword{
			.id = keywordID,
			.name = "Runtime Feature",
			.referenceName = "RUNTIME_FEATURE",
			.defaultIndex = 1,
			.runtimeToggle = true,
			});
		keywordGraph.nodes.emplace_back(Engine::ShaderGraphNode{
			.id = keywordNodeID,
			.kind = Engine::ShaderGraphNodeKind::Keyword,
			.keywordID = keywordID,
			});
		keywordGraph.links.emplace_back(Engine::ShaderGraphLink{
			.id = Engine::UUID::New(),
			.outputNode = keywordNodeID,
			.inputNode = keywordGraph.outputNode,
			.inputSlot = 2,
			});
		const Engine::ShaderGraphCompileOutput runtimeKeywordOutput =
			Engine::ShaderGraphCompiler::Compile(
				keywordGraph, "NEMKeywordTest.surface.hlsli");
		const std::string runtimeKeywordName =
			runtimeKeywordOutput.parameters.empty() ? std::string{} :
			runtimeKeywordOutput.parameters.front().shaderName;
		if (!runtimeKeywordOutput.Succeeded() ||
			runtimeKeywordOutput.parameters.size() != 1 ||
			runtimeKeywordOutput.surfaceHLSL.find(
				"uint " + runtimeKeywordName + ";") ==
				std::string::npos ||
			runtimeKeywordOutput.surfaceHLSL.find(
				"graphParameters." + runtimeKeywordName) == std::string::npos) {
			return false;
		}
		if (!writeGeneratedGraph(keywordGraph, "RuntimeKeyword")) {
			return false;
		}
		keywordGraph.keywords.front().runtimeToggle = false;
		const Engine::ShaderGraphCompileOutput staticKeywordOutput =
			Engine::ShaderGraphCompiler::Compile(
				keywordGraph, "NEMKeywordTest.surface.hlsli");
		if (!staticKeywordOutput.Succeeded() ||
			!staticKeywordOutput.parameters.empty() ||
			staticKeywordOutput.surfaceHLSL.find(
				"uint " + runtimeKeywordName + ";") !=
				std::string::npos ||
			staticKeywordOutput.surfaceHLSL.find("1u") == std::string::npos) {
			return false;
		}

		const Engine::ShaderGraphAsset postProcessGraph =
			Engine::CreateDefaultPostProcessShaderGraph("NEMPostProcess");
		const Engine::ShaderGraphCompileOutput postProcessOutput =
			Engine::ShaderGraphCompiler::Compile(
				postProcessGraph, "NEMPostProcess.generated.hlsli");
		Engine::ShaderGraphAsset restoredPostProcess{};
		if (!postProcessOutput.Succeeded() ||
			postProcessOutput.computeHLSL.find("[numthreads(8, 8, 1)]") ==
				std::string::npos ||
			postProcessOutput.computeHLSL.find("gSourceColor.SampleLevel") ==
				std::string::npos ||
			!Engine::FromJson(
				Engine::ToJson(postProcessGraph), restoredPostProcess) ||
			restoredPostProcess.domain !=
				Engine::ShaderGraphDomain::PostProcess) {
			return false;
		}

		const Engine::ShaderGraphAsset rayTracingGraph =
			Engine::CreateDefaultRayTracingEffectShaderGraph(
				"NEMRayTracingFeature");
		const Engine::ShaderGraphCompileOutput rayTracingOutput =
			Engine::ShaderGraphCompiler::Compile(
				rayTracingGraph, "NEMRayTracingFeature.generated.hlsli");
		Engine::ShaderGraphAsset restoredRayTracing{};
		if (!rayTracingOutput.Succeeded() ||
			rayTracingOutput.rayTracingHLSL.find(
				"RenderFeatureRayGeneration") == std::string::npos ||
			rayTracingOutput.rayTracingHLSL.find("TraceRay(") ==
				std::string::npos ||
			!Engine::FromJson(Engine::ToJson(rayTracingGraph),
				restoredRayTracing) ||
			restoredRayTracing.domain !=
				Engine::ShaderGraphDomain::RayTracingEffect ||
			!writeGeneratedGraph(rayTracingGraph,
				"RayTracingFeature")) {

			return false;
		}

		constexpr std::array targetIncludes{
			std::pair{
				Engine::ShaderGraphTarget::Primitive3D,
				"Builtin/Primitive/primitive.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Sprite,
				"Builtin/Sprite/defaultSprite.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Text,
				"Builtin/Text/defaultText.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Primitive2D,
					"Builtin/Primitive/primitive2D.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Particle,
					"Builtin/Particle/Common/particle.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Trail,
					"Builtin/Particle/Common/particle.hlsli" },
		};
		for (const auto& [target, include] : targetIncludes) {
			const Engine::ShaderGraphAsset targetGraph =
				Engine::CreateDefaultSurfaceShaderGraph(
					"NEMTargetTest", target);
			const Engine::ShaderGraphCompileOutput targetOutput =
				Engine::ShaderGraphCompiler::Compile(
					targetGraph,
					"NEMTargetTest.surface.hlsli");
			if (!targetOutput.Succeeded() ||
				targetOutput.opaquePixelHLSL.find(include) ==
					std::string::npos ||
				((target == Engine::ShaderGraphTarget::Particle ||
					target == Engine::ShaderGraphTarget::Trail) &&
					targetOutput.opaquePixelHLSL.find(
						"PrepareParticleBlendColor") == std::string::npos) ||
				targetGraph.nodes.size() !=
					(Engine::IsShaderGraph3DTarget(target) ?
						8u : 4u)) {

				return false;
			}
			if (!writeGeneratedGraph(
				targetGraph,
				Engine::EnumAdapter<
					Engine::ShaderGraphTarget>::
				ToString(target))) {

				return false;
			}
			Engine::ShaderGraphAsset restoredTarget{};
			if (!Engine::FromJson(
				Engine::ToJson(targetGraph),
				restoredTarget) ||
				restoredTarget.target != target) {

				return false;
			}
		}

		// Sampler Stateはノード単位の静的サンプラーとして保存、登録する
		{
			Engine::ShaderGraphAsset samplerGraph =
				Engine::CreateDefaultSurfaceShaderGraph(
					"NEMSamplerTest");
			std::erase_if(
				samplerGraph.links,
				[&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == samplerGraph.outputNode &&
						link.inputSlot == 0;
				});

			Engine::MaterialParameterValue textureValue{};
			textureValue.value = Engine::AssetID{};
			const Engine::UUID textureParameter = Engine::UUID::New();
			samplerGraph.parameters.emplace_back(
				Engine::ShaderGraphParameter{
					.id = textureParameter,
					.name = "SamplerTexture",
					.type = Engine::ShaderGraphValueType::Texture2D,
					.defaultValue = textureValue,
				});

			const Engine::UUID textureNode = Engine::UUID::New();
			const Engine::UUID uvNode = Engine::UUID::New();
			const Engine::UUID samplerNode = Engine::UUID::New();
			const Engine::UUID sampleNode = Engine::UUID::New();
			Engine::ShaderGraphNode sampler{
				.id = samplerNode,
				.kind = Engine::ShaderGraphNodeKind::SamplerState,
			};
			sampler.sampler.filter =
				D3D12_FILTER_ANISOTROPIC;
			sampler.sampler.addressU =
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.sampler.addressV =
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			sampler.sampler.maxAnisotropy = 8;
			samplerGraph.nodes.insert(
				samplerGraph.nodes.end(), {
					Engine::ShaderGraphNode{
						.id = textureNode,
						.kind = Engine::ShaderGraphNodeKind::Parameter,
						.parameterID = textureParameter,
						.valueType = Engine::ShaderGraphValueType::Texture2D,
					},
					Engine::ShaderGraphNode{
						.id = uvNode,
						.kind = Engine::ShaderGraphNodeKind::UV,
					},
					sampler,
					Engine::ShaderGraphNode{
						.id = sampleNode,
						.kind = Engine::ShaderGraphNodeKind::TextureSample,
					},
				});
			const auto addSamplerLink =
				[&](Engine::UUID source,
					Engine::UUID destination,
					uint32_t destinationSlot) {

					samplerGraph.links.emplace_back(
						Engine::ShaderGraphLink{
							.id = Engine::UUID::New(),
							.outputNode = source,
							.inputNode = destination,
							.inputSlot = destinationSlot,
						});
				};
			addSamplerLink(textureNode, sampleNode, 0);
			addSamplerLink(uvNode, sampleNode, 1);
			addSamplerLink(samplerNode, sampleNode, 2);
			addSamplerLink(sampleNode, samplerGraph.outputNode, 0);

			const Engine::ShaderGraphCompileOutput samplerOutput =
				Engine::ShaderGraphCompiler::Compile(
					samplerGraph,
					"NEMSamplerTest.surface.hlsli");
			if (!samplerOutput.Succeeded() ||
				samplerOutput.samplers.size() != 1 ||
				samplerOutput.samplers.front().node != samplerNode ||
				samplerOutput.samplers.front().shaderRegister != 2 ||
				samplerOutput.samplers.front().settings.filter !=
					D3D12_FILTER_ANISOTROPIC ||
				samplerOutput.surfaceHLSL.find(
					"register(s2)") == std::string::npos ||
				samplerOutput.surfaceHLSL.find(
					samplerOutput.samplers.front().shaderName) ==
					std::string::npos) {

				return false;
			}
			if (!writeGeneratedGraph(
				samplerGraph, "Sampler")) {

				return false;
			}

			Engine::ShaderGraphAsset restoredSampler{};
			if (!Engine::FromJson(
				Engine::ToJson(samplerGraph), restoredSampler)) {

				return false;
			}
			const auto restoredSamplerNode = std::ranges::find_if(
				restoredSampler.nodes,
				[&](const Engine::ShaderGraphNode& node) {
					return node.id == samplerNode;
				});
			if (restoredSamplerNode == restoredSampler.nodes.end() ||
				restoredSamplerNode->sampler.filter !=
					D3D12_FILTER_ANISOTROPIC ||
				restoredSamplerNode->sampler.addressU !=
					D3D12_TEXTURE_ADDRESS_MODE_CLAMP ||
				restoredSamplerNode->sampler.addressV !=
					D3D12_TEXTURE_ADDRESS_MODE_MIRROR ||
				restoredSamplerNode->sampler.maxAnisotropy != 8) {

				return false;
			}
		}

		constexpr std::array vertexTargets{
			Engine::ShaderGraphTarget::Mesh,
			Engine::ShaderGraphTarget::Primitive3D,
			Engine::ShaderGraphTarget::Primitive2D,
		};
		for (const Engine::ShaderGraphTarget target :
			vertexTargets) {

			Engine::ShaderGraphAsset vertexGraph =
				Engine::CreateDefaultSurfaceShaderGraph(
					"NEMVertexTargetTest", target);
			const Engine::UUID vertexOutput =
				Engine::UUID::New();
			vertexGraph.nodes.emplace_back(
				Engine::ShaderGraphNode{
					.id = vertexOutput,
					.kind = Engine::ShaderGraphNodeKind::VertexOutput,
				});
			vertexGraph.vertexOutputNode = vertexOutput;

			Engine::MaterialParameterValue textureValue{};
			textureValue.value = Engine::AssetID{};
			const Engine::UUID textureParameter =
				Engine::UUID::New();
			vertexGraph.parameters.emplace_back(
				Engine::ShaderGraphParameter{
					.id = textureParameter,
					.name = "DisplacementTexture",
					.type = Engine::ShaderGraphValueType::Texture2D,
					.defaultValue = textureValue,
				});
			const Engine::UUID textureNode = Engine::UUID::New();
			const Engine::UUID uvNode = Engine::UUID::New();
			const Engine::UUID timeNode = Engine::UUID::New();
			const Engine::UUID uvAddNode = Engine::UUID::New();
			const Engine::UUID sampleNode = Engine::UUID::New();
			const Engine::UUID positionNode = Engine::UUID::New();
			const Engine::UUID positionAddNode = Engine::UUID::New();
			vertexGraph.nodes.insert(
				vertexGraph.nodes.end(), {
					Engine::ShaderGraphNode{
						.id = textureNode,
						.kind = Engine::ShaderGraphNodeKind::Parameter,
						.parameterID = textureParameter,
						.valueType = Engine::ShaderGraphValueType::Texture2D,
					},
					Engine::ShaderGraphNode{
						.id = uvNode,
						.kind = Engine::ShaderGraphNodeKind::UV,
					},
					Engine::ShaderGraphNode{
						.id = timeNode,
						.kind = Engine::ShaderGraphNodeKind::Time,
					},
					Engine::ShaderGraphNode{
						.id = uvAddNode,
						.kind = Engine::ShaderGraphNodeKind::Add,
					},
					Engine::ShaderGraphNode{
						.id = sampleNode,
						.kind = Engine::ShaderGraphNodeKind::TextureSample,
					},
					Engine::ShaderGraphNode{
						.id = positionNode,
						.kind = Engine::ShaderGraphNodeKind::ObjectPosition,
					},
					Engine::ShaderGraphNode{
						.id = positionAddNode,
						.kind = Engine::ShaderGraphNodeKind::Add,
					},
				});
			const auto addVertexLink =
				[&](Engine::UUID source, uint32_t sourceSlot,
					Engine::UUID destination, uint32_t destinationSlot) {

				vertexGraph.links.emplace_back(
					Engine::ShaderGraphLink{
						.id = Engine::UUID::New(),
						.outputNode = source,
						.outputSlot = sourceSlot,
						.inputNode = destination,
						.inputSlot = destinationSlot,
					});
			};
			addVertexLink(uvNode, 0, uvAddNode, 0);
			addVertexLink(timeNode, 0, uvAddNode, 1);
			addVertexLink(textureNode, 0, sampleNode, 0);
			addVertexLink(uvAddNode, 0, sampleNode, 1);
			addVertexLink(positionNode, 0, positionAddNode, 0);
			addVertexLink(sampleNode, 2, positionAddNode, 1);
			addVertexLink(positionAddNode, 0, vertexOutput, 0);
			const Engine::ShaderGraphCompileOutput vertexOutputResult =
				Engine::ShaderGraphCompiler::Compile(
					vertexGraph,
					"NEMVertexTargetTest.surface.hlsli");
			const bool expectsMeshShader =
				target != Engine::ShaderGraphTarget::Primitive2D;
			// Primitiveの生成PSも標準描画と同じRingのUV補正を使う
			if (target != Engine::ShaderGraphTarget::Mesh &&
				(vertexOutputResult.opaquePixelHLSL.find("ResolvePrimitivePixelUV") == std::string::npos ||
					vertexOutputResult.transparentPixelHLSL.find("ResolvePrimitivePixelUV") == std::string::npos)) {
				return false;
			}
			const std::string expectedFunction =
				target == Engine::ShaderGraphTarget::Mesh ?
					"EvaluateShaderGraphVertex" :
					(target == Engine::ShaderGraphTarget::Primitive3D ?
						"EvaluatePrimitiveShaderGraphVertex" :
						"EvaluatePrimitive2DShaderGraphVertex");
			if (!Engine::SupportsShaderGraphVertexOutput(target) ||
				!vertexOutputResult.Succeeded() ||
				vertexOutputResult.vertexHLSL.find(
					expectedFunction) == std::string::npos ||
				(expectsMeshShader !=
					!vertexOutputResult.meshHLSL.empty()) ||
				!writeGeneratedGraph(
					vertexGraph,
					std::string("Vertex") +
						std::string(Engine::EnumAdapter<
							Engine::ShaderGraphTarget>::
							ToString(target)))) {

				return false;
			}
		}

		constexpr std::array additionalNodeKinds{
			Engine::ShaderGraphNodeKind::Subtract,
			Engine::ShaderGraphNodeKind::Divide,
			Engine::ShaderGraphNodeKind::Power,
			Engine::ShaderGraphNodeKind::Sine,
			Engine::ShaderGraphNodeKind::Time,
			Engine::ShaderGraphNodeKind::Remap,
			Engine::ShaderGraphNodeKind::TilingAndOffset,
			Engine::ShaderGraphNodeKind::PolarCoordinates,
			Engine::ShaderGraphNodeKind::Split,
			Engine::ShaderGraphNodeKind::Combine,
			Engine::ShaderGraphNodeKind::Dither,
		};
		for (const Engine::ShaderGraphNodeKind kind :
			additionalNodeKinds) {

			Engine::ShaderGraphAsset nodeGraph =
				Engine::CreateDefaultSurfaceShaderGraph("NEMNodeTest");
			for (auto it = nodeGraph.links.begin();
				it != nodeGraph.links.end();) {

				if (it->inputNode == nodeGraph.outputNode &&
					it->inputSlot == 0) {

					it = nodeGraph.links.erase(it);
					continue;
				}
				++it;
			}

			const Engine::UUID nodeID = Engine::UUID::New();
			nodeGraph.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = nodeID,
				.kind = kind,
				.previewExpanded = false,
				});
			nodeGraph.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = nodeID,
				.inputNode = nodeGraph.outputNode,
				.inputSlot = 0,
				});

			const Engine::ShaderGraphCompileOutput nodeOutput =
				Engine::ShaderGraphCompiler::Compile(
					nodeGraph, "NEMNodeTest.surface.hlsli");
			if (!nodeOutput.Succeeded()) {
				return false;
			}

			Engine::ShaderGraphAsset restored{};
			if (!Engine::FromJson(Engine::ToJson(nodeGraph), restored)) {
				return false;
			}
			const Engine::ShaderGraphNode& restoredNode =
				restored.nodes.back();
			if (restoredNode.kind != kind ||
				restoredNode.previewExpanded) {

				return false;
			}
		}

		// 各成分数と画面座標の接続有無でディザを生成する
		for (const auto type : { Engine::ShaderGraphValueType::Float,
			Engine::ShaderGraphValueType::Float2, Engine::ShaderGraphValueType::Float3,
			Engine::ShaderGraphValueType::Float4 }) {

			for (const bool explicitPosition : { false, true }) {
				auto ditherGraph = Engine::CreateDefaultSurfaceShaderGraph("Dither");
				ditherGraph.renderState.alphaClipping = true;
				std::erase_if(ditherGraph.links, [&](const auto& link) {
					return link.inputNode == ditherGraph.outputNode && link.inputSlot >= 6;
				});
				auto addNode = [&](Engine::ShaderGraphNodeKind kind) {
					const auto id = Engine::UUID::New();
					ditherGraph.nodes.emplace_back(Engine::ShaderGraphNode{ .id = id, .kind = kind });
					return id;
				};
				auto connect = [&](Engine::UUID from, Engine::UUID to, uint32_t slot) {
					ditherGraph.links.emplace_back(Engine::ShaderGraphLink{
						.id = Engine::UUID::New(), .outputNode = from,
						.inputNode = to, .inputSlot = slot });
				};
				const auto strength = addNode(Engine::ShaderGraphNodeKind::Parameter);
				const auto parameterID = Engine::UUID::New();
				ditherGraph.parameters.emplace_back(Engine::ShaderGraphParameter{
					.id = parameterID, .name = "Strength", .type = type });
				ditherGraph.nodes.back().parameterID = parameterID;
				const auto saturate = addNode(Engine::ShaderGraphNodeKind::Saturate);
				const auto inverse = addNode(Engine::ShaderGraphNodeKind::OneMinus);
				const auto dither = addNode(Engine::ShaderGraphNodeKind::Dither);
				const auto step = addNode(Engine::ShaderGraphNodeKind::Step);
				const auto zero = addNode(Engine::ShaderGraphNodeKind::Constant);
				connect(strength, saturate, 0);
				connect(saturate, inverse, 0);
				connect(inverse, dither, 0);
				connect(dither, step, 1);
				connect(zero, step, 0);
				connect(step, ditherGraph.outputNode, 6);
				const auto threshold = addNode(Engine::ShaderGraphNodeKind::Constant);
				ditherGraph.nodes.back().value.value = 0.5f;
				connect(threshold, ditherGraph.outputNode, 7);
				// Maskedパスにもディザを含む生成シェーダーを適用する
				const Engine::AssetID ditherID{ 71, 82 };
				auto ditherMaterial = Engine::ShaderGraphArtifactCache::CreateMaterial(ditherGraph, ditherID);
				const auto ditherArtifact = Engine::ShaderGraphArtifactCache::DescribeReferences(ditherGraph, ditherID);
				Engine::ShaderGraphArtifactCache::ApplyToMaterial(ditherArtifact, ditherMaterial);
				const auto* masked = Engine::FindPass(ditherMaterial, Engine::MaterialPassKind::Masked);
				if (ditherMaterial.renderState.surfaceMode != Engine::MaterialSurfaceMode::Masked ||
					!masked || masked->pipeline != ditherArtifact.opaquePipelineID ||
					masked->shaderOverride != ditherArtifact.opaqueShaderID) return false;
				if (explicitPosition) {
					connect(addNode(Engine::ShaderGraphNodeKind::ScreenPosition), dither, 1);
				}
				const auto compiled = Engine::ShaderGraphCompiler::Compile(ditherGraph, "surface.hlsli");
				if (compiled.vertexHLSL.empty() || compiled.meshHLSL.empty() ||
					compiled.opaquePixelHLSL.find("StructuredBuffer<ShaderGraphParameters> gMeshMaterialParameters") == std::string::npos ||
					compiled.vertexHLSL.find("#define NEM_SHADER_GRAPH_MATERIAL") == std::string::npos ||
					compiled.meshHLSL.find("#define NEM_SHADER_GRAPH_MATERIAL") == std::string::npos) {
					return false;
				}
				if (!compiled.Succeeded() ||
					compiled.surfaceHLSL.find("ShaderGraphDitherThreshold((graphInput.screenPosition).xy)") == std::string::npos ||
					compiled.surfaceHLSL.find("cell.x * 4u + cell.y") == std::string::npos ||
					compiled.surfaceHLSL.find(" / 17.0f") == std::string::npos ||
					compiled.opaquePixelHLSL.find("clip(graph.baseColor.a * graph.opacity - graph.alphaClip)") == std::string::npos ||
					compiled.depthPixelHLSL.find("clip(graph.baseColor.a * graph.opacity - graph.alphaClip)") == std::string::npos ||
					compiled.pickingPixelHLSL.find("clip(graph.baseColor.a * graph.opacity - graph.alphaClip)") == std::string::npos) {

					return false;
				}
				Engine::ShaderGraphAsset restored;
				if (!Engine::FromJson(Engine::ToJson(ditherGraph), restored) ||
					!writeGeneratedGraph(restored, std::string("Dither") +
						std::string(Engine::EnumAdapter<Engine::ShaderGraphValueType>::ToString(type)) +
						(explicitPosition ? "Explicit" : "Default"))) {

					return false;
				}
				// 実際のリフレクションとGPU転送用レイアウトで公開値を検証する
				if (type == Engine::ShaderGraphValueType::Float && !explicitPosition) {
					// テストではプロジェクト探索に依存せずDXCへ明示パスを渡す
					const auto releaseLibrary = [](void* module) { FreeLibrary(static_cast<HMODULE>(module)); };
					std::unique_ptr<void, decltype(releaseLibrary)> library(LoadLibraryW(L"dxcompiler.dll"), releaseLibrary);
					if (!library) {
						return false;
					}
					const auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(
						GetProcAddress(static_cast<HMODULE>(library.get()), "DxcCreateInstance"));
					ComPtr<IDxcUtils> utils;
					ComPtr<IDxcCompiler3> compiler;
					ComPtr<IDxcIncludeHandler> includes;
					ComPtr<IDxcBlobEncoding> source;
					if (!createInstance || FAILED(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
						FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) ||
						FAILED(utils->CreateDefaultIncludeHandler(&includes)) ||
						FAILED(utils->LoadFile((generatedRoot / "DitherFloatDefault/opaque.PS.hlsl").c_str(), nullptr, &source))) {
						return false;
					}
					const auto graphInclude = (generatedRoot / "DitherFloatDefault").wstring();
					const auto engineInclude = shaderRoot.wstring();
					std::array arguments{ L"-T", L"ps_6_6", L"-E", L"main",
						L"-I", engineInclude.c_str(), L"-I", graphInclude.c_str() };
					const DxcBuffer input{ source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8 };
					ComPtr<IDxcResult> result;
					HRESULT status = E_FAIL;
					ComPtr<IDxcBlob> reflectionBlob;
					if (FAILED(compiler->Compile(&input, arguments.data(), static_cast<UINT32>(arguments.size()),
						includes.Get(), IID_PPV_ARGS(&result))) || FAILED(result->GetStatus(&status)) || FAILED(status) ||
						FAILED(result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr))) {
						return false;
					}
					Engine::CompiledShader shader;
					const DxcBuffer reflectionInput{ reflectionBlob->GetBufferPointer(), reflectionBlob->GetBufferSize(), 0 };
					if (!Engine::ParseDxShaderReflection(utils.Get(), reflectionInput, Engine::ShaderStage::PS, shader.reflection)) {
						return false;
					}
					Engine::ShaderAsset metadata;
					metadata.parameters = compiled.parameters;
					Engine::ApplyShaderParameterMetadata(shader.reflection, metadata);
					Engine::MaterialParameterLayout layout;
					layout.Build(shader.reflection, Engine::MaterialParameterCBuffer::kMesh);
					const auto* variable = layout.Find(compiled.parameters.front().id);
					if (!variable || !variable->used ||
						variable->name != "Strength" || variable->valueType != D3D_SVT_FLOAT) {
						return false;
					}
					Engine::MaterialParameterSet overrides;
					overrides.Set(variable->parameterID, variable->name,
						variable->semantic, Engine::MaterialParameterValue{ .value = 0.75f });
					const auto packed = Engine::MaterialParameterBufferBuilder::BuildElement(
						{}, overrides, layout, {});
					float strengthValue = 0.0f;
					if (packed.size() < variable->offset + sizeof(strengthValue)) {
						return false;
					}
					std::memcpy(&strengthValue, packed.data() + variable->offset, sizeof(strengthValue));
					if (strengthValue != 0.75f) {
						return false;
					}
				}
				const auto vertex = addNode(Engine::ShaderGraphNodeKind::VertexOutput);
				ditherGraph.vertexOutputNode = vertex;
				connect(dither, vertex, 0);
				if (Engine::ShaderGraphCompiler::Compile(ditherGraph, "surface.hlsli").Succeeded()) {
					return false;
				}
			}
		}

		// しきい値全域で端点と表示数の単調性を確認する
		int previousVisible = 16;
		for (int strengthStep = 0; strengthStep <= 100; ++strengthStep) {
			int visible = 0;
			for (int cell = 1; cell <= 16; ++cell) {
				visible += 1.0f - strengthStep / 100.0f >= cell / 17.0f ? 1 : 0;
			}
			if (visible > previousVisible || (strengthStep == 0 && visible != 16) ||
				(strengthStep == 100 && visible != 0)) {
				return false;
			}
			previousVisible = visible;
		}

		constexpr std::array timeExpressions{
			"(shaderGraphTime).xxxx",
			"(sin(shaderGraphTime)).xxxx",
			"(cos(shaderGraphTime)).xxxx",
			"(shaderGraphDeltaTime).xxxx",
			"(shaderGraphSmoothDeltaTime).xxxx",
		};
		for (uint32_t outputSlot = 0;
			outputSlot < timeExpressions.size();
			++outputSlot) {

			Engine::ShaderGraphAsset timeGraph =
				Engine::CreateDefaultSurfaceShaderGraph("NEMTimeTest");
			std::erase_if(
				timeGraph.links,
				[&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == timeGraph.outputNode &&
						link.inputSlot == 0;
				});
			const Engine::UUID timeNodeID = Engine::UUID::New();
			timeGraph.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = timeNodeID,
				.kind = Engine::ShaderGraphNodeKind::Time,
				.previewExpanded = false,
				});
			timeGraph.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = timeNodeID,
				.outputSlot = outputSlot,
				.inputNode = timeGraph.outputNode,
				.inputSlot = 0,
				});
			const Engine::ShaderGraphCompileOutput timeOutput =
				Engine::ShaderGraphCompiler::Compile(
					timeGraph, "NEMTimeTest.surface.hlsli");
			if (!timeOutput.Succeeded() ||
				timeOutput.surfaceHLSL.find(
					timeExpressions[outputSlot]) ==
					std::string::npos) {

				return false;
			}
		}

		// Sub Graphは公開パラメータを入力、参照先Outputを出力として展開する
		{
			Engine::ShaderGraphAsset child =
				Engine::CreateDefaultSurfaceShaderGraph("NEMSubGraph");
			std::erase_if(child.links,
				[&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == child.outputNode &&
						link.inputSlot == 0;
				});
			const Engine::UUID parameterID = Engine::UUID::New();
			child.parameters.emplace_back(Engine::ShaderGraphParameter{
				.id = parameterID,
				.name = "Color",
				.type = Engine::ShaderGraphValueType::Color,
				.defaultValue = Engine::MaterialParameterValue{
					.value = Engine::Color4::White(),
					},
				});
			const Engine::UUID parameterNode = Engine::UUID::New();
			child.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = parameterNode,
				.kind = Engine::ShaderGraphNodeKind::Parameter,
				.parameterID = parameterID,
				.valueType = Engine::ShaderGraphValueType::Color,
				});
			child.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = parameterNode,
				.inputNode = child.outputNode,
				.inputSlot = 0,
				});

			Engine::ShaderGraphAsset parent =
				Engine::CreateDefaultSurfaceShaderGraph("NEMSubGraphParent");
			const Engine::UUID colorNode = parent.links.front().outputNode;
			std::erase_if(parent.links,
				[&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == parent.outputNode &&
						link.inputSlot == 0;
				});
			const Engine::UUID subGraphNode = Engine::UUID::New();
			const Engine::AssetID subGraphID{ 10, 20 };
			parent.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = subGraphNode,
				.kind = Engine::ShaderGraphNodeKind::SubGraph,
				.subGraph = subGraphID,
				});
			parent.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = colorNode,
				.inputNode = subGraphNode,
				.inputSlot = 0,
				});
			parent.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = subGraphNode,
				.outputSlot = 0,
				.inputNode = parent.outputNode,
				.inputSlot = 0,
				});
			const Engine::ShaderGraphCompileOutput subGraphOutput =
				Engine::ShaderGraphCompiler::Compile(
					parent, "NEMSubGraph.surface.hlsli",
					[&](Engine::AssetID id, Engine::ShaderGraphAsset& outGraph) {
						if (id != subGraphID) {
							return false;
						}
						outGraph = child;
						return true;
					});
			if (!subGraphOutput.Succeeded() ||
				subGraphOutput.surfaceHLSL.find("NEMSubGraph") !=
					std::string::npos) {
				return false;
			}
		}

		graph.nodes[1].id = graph.nodes[0].id;
		const Engine::ShaderGraphCompileOutput invalid =
			Engine::ShaderGraphCompiler::Compile(
				graph, "NEMTest.surface.hlsli");
		return !invalid.Succeeded() &&
			!invalid.diagnostics.empty();
	}
}
