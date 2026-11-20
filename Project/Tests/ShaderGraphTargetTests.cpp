#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactBuilder.h>
#include <Engine/Core/Rendering/Shaders/ShaderCookStorage.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>

namespace {

	// 非公開設定を保存形式とGPU配置へ引き継ぐ
	bool TestParameterVisibility() {

		using namespace Engine;
		const auto colorID = MaterialParameterID::FromName("PrivateColor");
		const auto textureID = MaterialParameterID::FromName("PrivateMap");
		const auto optimizedID = MaterialParameterID::FromName("OptimizedPrivate");
		ShaderAsset shader;
		shader.guid = {57, 59};
		shader.parameters = {
			{.shaderName = "Tint", .id = MaterialParameterID::FromName("Tint")},
			{.shaderName = "PrivateColor", .displayName = "内部色", .id = colorID, .isColor = true, .exposed = false},
			{.shaderName = "PrivateMap", .id = textureID, .semantic = MaterialParameterSemantic::BaseColorTexture,
				.isTexture = true, .exposed = false},
			{.shaderName = "OptimizedPrivate", .id = optimizedID, .exposed = false},
		};
		ShaderAsset restored;
		if (!FromJson(ToJson(shader), restored) || restored.parameters.size() != 4 ||
			!restored.parameters[0].exposed || restored.parameters[1].exposed ||
			restored.parameters[2].exposed || restored.parameters[3].exposed) return false;
		auto legacy = ToJson(shader);
		legacy["parameters"][1].erase("exposed");
		if (!FromJson(legacy, restored) || !restored.parameters[1].exposed) return false;
		if (!ShaderCookStorage::ReadShaderMetadata(ShaderCookStorage::WriteShaderMetadata(shader), restored) ||
			restored.guid != shader.guid || restored.parameters.size() != 4 || restored.parameters[1].exposed ||
			restored.parameters[2].exposed || restored.parameters[3].exposed) return false;

		ShaderReflectionInfo reflection;
		const ShaderConstantBufferVariable variable{
			.name = "PrivateColor", .size = 16, .valueClass = D3D_SVC_VECTOR, .valueType = D3D_SVT_FLOAT,
			.rows = 1, .columns = 4, .declaredComponentCount = 4, .declaredByteSize = 16,
		};
		reflection.constantBuffers.emplace_back(ShaderConstantBufferInfo{
			.name = "MaterialParameters", .size = 16, .variables = {variable},
		});
		reflection.structuredBuffers.emplace_back(ShaderStructuredBufferInfo{
			.name = "gMeshMaterialParameters", .stride = 16, .variables = {variable},
		});
		reflection.resources.emplace_back(ShaderResourceBinding{.name = "PrivateMap", .kind = ShaderBindingKind::SRV});
		ApplyShaderParameterMetadata(reflection, shader);
		const auto& constant = reflection.constantBuffers.front().variables.front();
		const auto& structured = reflection.structuredBuffers.front().variables.front();
		const auto& resource = reflection.resources.front();
		if (constant.parameterID != colorID || constant.exposed || !constant.isColor || constant.name != "内部色" ||
			structured.parameterID != colorID || structured.exposed || resource.parameterID != textureID ||
			resource.exposed || resource.name != "PrivateMap" ||
			resource.semantic != MaterialParameterSemantic::BaseColorTexture || reflection.hiddenParameters.size() != 3 ||
			std::ranges::find(reflection.hiddenParameters, optimizedID) == reflection.hiddenParameters.end()) return false;
		ShaderReflectionInfo cooked;
		if (!ShaderCookStorage::ReadReflection(ShaderCookStorage::WriteReflection(reflection), cooked) ||
			cooked.hiddenParameters != reflection.hiddenParameters || cooked.constantBuffers.size() != 1 ||
			cooked.structuredBuffers.size() != 1 || cooked.resources.size() != 1 ||
			cooked.constantBuffers.front().variables.front().exposed ||
			cooked.constantBuffers.front().variables.front().parameterID != colorID ||
			cooked.structuredBuffers.front().variables.front().exposed ||
			cooked.resources.front().exposed || cooked.resources.front().parameterID != textureID) return false;

		// 複数Stageの統合でも最適化された内部値を保持する
		ShaderReflectionInfo merged;
		MergeShaderReflection(merged, cooked);
		MergeShaderReflection(merged, cooked);
		if (merged.hiddenParameters != cooked.hiddenParameters || merged.constantBuffers.size() != 1 ||
			merged.constantBuffers.front().variables.front().exposed || merged.resources.size() != 1 ||
			merged.resources.front().exposed) return false;
		MaterialParameterLayout layout;
		layout.Build(cooked);
		MaterialParameterSet defaults;
		const Vector4 color(.2f, .4f, .6f, .75f);
		defaults.Set(colorID, "PrivateColor", MaterialParameterSemantic::None, {color});
		const auto bytes = MaterialParameterBufferBuilder::BuildElement(defaults, {}, layout, {});
		Vector4 packed{};
		if (!layout.IsValid() || bytes.size() < sizeof(packed)) return false;
		std::memcpy(&packed, bytes.data(), sizeof(packed));
		return packed == color;
	}
}

namespace NEMTests {

	bool TestShaderGraphShaderDefinitions() {

		if (!TestParameterVisibility()) return false;
		using namespace Engine;
		const AssetID shaderID{57, 58};
		const std::filesystem::path path = "generated.hlsl";
		const std::vector<ShaderParameterMetadata> parameters{
			{.shaderName = "Tint", .displayName = "色", .id = MaterialParameterID::FromName("Tint"), .isColor = true},
			{.shaderName = "Map", .displayName = "画像", .id = MaterialParameterID::FromName("Map"), .isTexture = true},
		};
		const ShaderAsset pixel = ShaderGraphArtifactBuilder::MakePixelShader("Pixel", shaderID, path, "shade", parameters);
		const ShaderAsset compute = ShaderGraphArtifactBuilder::MakeComputeShader("Compute", shaderID, path, parameters);
		const ShaderAsset surface =
			ShaderGraphArtifactBuilder::MakeRayTracingShader("Surface", shaderID, path, parameters, false);
		const ShaderAsset effect = ShaderGraphArtifactBuilder::MakeRayTracingShader("Effect", shaderID, path, parameters, true);

		// 各段階で安定IDと編集属性を引き継ぐ
		for (const ShaderAsset* shader : {&pixel, &compute, &surface, &effect}) {
			if (shader->guid != shaderID || shader->parameters.size() != 2 ||
				shader->colorParameters != std::vector<std::string>{"Tint"} || shader->parameters[0].id != parameters[0].id ||
				!shader->parameters[0].isColor || shader->parameters[1].id != parameters[1].id ||
				!shader->parameters[1].isTexture) {
				return false;
			}
			for (const auto& stage : shader->stages) {
				if (stage.file != "generated.hlsl" || stage.ownerShader) {
					return false;
				}
			}
		}
		// PixelとComputeの入口とProfileを維持する
		if (pixel.stages.size() != 1 || pixel.stages[0].stage != ShaderStage::PS || pixel.stages[0].entry != "shade" ||
			pixel.stages[0].profile != "ps_6_0" || compute.stages.size() != 1 || compute.stages[0].stage != ShaderStage::CS ||
			compute.stages[0].entry != "main" || compute.stages[0].profile != "cs_6_0") {
			return false;
		}
		// ShaderTableで使うExport順を維持する
		const std::vector<std::string> surfaceEntries{"ReflectionClosestHit", "ReflectionAnyHit"};
		const std::vector<std::string> effectEntries{"RenderFeatureRayGeneration", "ReflectionMiss", "ReflectionClosestHit", "ReflectionAnyHit"};
		for (const auto& [shader, entries] : {std::pair{&surface, &surfaceEntries}, std::pair{&effect, &effectEntries}}) {
			if (shader->stages.size() != entries->size()) {
				return false;
			}
			for (size_t index = 0; index < entries->size(); ++index) {
				if (shader->stages[index].stage != ShaderStage::Lib || shader->stages[index].profile != "lib_6_6" ||
					shader->stages[index].entry != (*entries)[index]) {
					return false;
				}
			}
		}
		return true;
	}

	// 描画対象ごとのShaderと保存値を確認する
	bool TestShaderGraphRasterTargets(const ShaderGraphCompileFixture& fixture) {

		constexpr std::array targetIncludes{
			std::pair{Engine::ShaderGraphTarget::Primitive3D, "Builtin/Primitive/primitive.hlsli"},
			std::pair{Engine::ShaderGraphTarget::Sprite, "Builtin/Sprite/defaultSprite.hlsli"},
			std::pair{Engine::ShaderGraphTarget::Text, "Builtin/Text/defaultText.hlsli"},
			std::pair{Engine::ShaderGraphTarget::Primitive2D, "Builtin/Primitive/primitive2D.hlsli"},
			std::pair{Engine::ShaderGraphTarget::Particle, "Builtin/Particle/Common/particle.hlsli"},
			std::pair{Engine::ShaderGraphTarget::Trail, "Builtin/Particle/Trail/particleTrail.hlsli"},
		};
		for (const auto& [target, include] : targetIncludes) {
			const Engine::ShaderGraphAsset targetGraph = Engine::CreateDefaultSurfaceShaderGraph("NEMTargetTest", target);
			const Engine::ShaderGraphCompileOutput targetOutput =
				Engine::ShaderGraphCompiler::Compile(targetGraph, "NEMTargetTest.surface.hlsli");
			if (!targetOutput.Succeeded() || targetOutput.opaquePixelHLSL.find(include) == std::string::npos ||
				((target == Engine::ShaderGraphTarget::Sprite || target == Engine::ShaderGraphTarget::Primitive2D) &&
					(targetOutput.outlinePixelHLSL.find(include) == std::string::npos ||
						targetOutput.outlinePixelHLSL.find("EvaluateShaderGraphSurface") == std::string::npos)) ||
				((target == Engine::ShaderGraphTarget::Particle || target == Engine::ShaderGraphTarget::Trail) &&
					targetOutput.opaquePixelHLSL.find("PrepareParticleBlendColor") == std::string::npos) ||
				targetGraph.nodes.size() != (Engine::IsShaderGraph3DTarget(target) ? 8u : 4u)) {

				return false;
			}
			if (target == Engine::ShaderGraphTarget::Sprite || target == Engine::ShaderGraphTarget::Primitive2D) {

				const Engine::MaterialAsset material = Engine::ShaderGraphArtifactCache::CreateMaterial(
					targetGraph, Engine::AssetID{41, static_cast<uint64_t>(target) + 1u});
				if (!Engine::FindPass(material, Engine::MaterialPassKind::ScreenSpaceOutlineMask) ||
					!Engine::FindPass(material, Engine::MaterialPassKind::ScreenSpaceOutlineCoverageMask)) {

					return false;
				}
			}
			if (!fixture.WriteGeneratedGraph(targetGraph, Engine::EnumAdapter<Engine::ShaderGraphTarget>::ToString(target))) {

				return false;
			}
			Engine::ShaderGraphAsset restoredTarget{};
			if (!Engine::FromJson(Engine::ToJson(targetGraph), restoredTarget) || restoredTarget.target != target) {

				return false;
			}
		}

		return true;
	}

	// 静的Samplerの登録と保存値を確認する
	bool TestShaderGraphStaticSamplers(const ShaderGraphCompileFixture& fixture) {

		// Sampler Stateはノード単位の静的サンプラーとして保存、登録する
		{
			Engine::ShaderGraphAsset samplerGraph = Engine::CreateDefaultSurfaceShaderGraph("NEMSamplerTest");
			std::erase_if(samplerGraph.links, [&](const Engine::ShaderGraphLink& link) {
				return link.inputNode == samplerGraph.outputNode && link.inputSlot == 0;
			});

			Engine::MaterialParameterValue textureValue{};
			textureValue.value = Engine::AssetID{};
			const Engine::UUID textureParameter = Engine::UUID::New();
			samplerGraph.parameters.emplace_back(Engine::ShaderGraphParameter{
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
			sampler.sampler.filter = D3D12_FILTER_ANISOTROPIC;
			sampler.sampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.sampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			sampler.sampler.maxAnisotropy = 8;
			samplerGraph.nodes.insert(samplerGraph.nodes.end(), {
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
			const auto addSamplerLink = [&](Engine::UUID source, Engine::UUID destination, uint32_t destinationSlot) {
				samplerGraph.links.emplace_back(Engine::ShaderGraphLink{
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
				Engine::ShaderGraphCompiler::Compile(samplerGraph, "NEMSamplerTest.surface.hlsli");
			if (!samplerOutput.Succeeded() || samplerOutput.samplers.size() != 1 ||
				samplerOutput.samplers.front().node != samplerNode || samplerOutput.samplers.front().shaderRegister != 2 ||
				samplerOutput.samplers.front().settings.filter != D3D12_FILTER_ANISOTROPIC ||
				samplerOutput.surfaceHLSL.find("HexTilingData hexGrid_") != std::string::npos ||
				samplerOutput.surfaceHLSL.find("register(s2)") == std::string::npos ||
				samplerOutput.surfaceHLSL.find(samplerOutput.samplers.front().shaderName) == std::string::npos) {

				return false;
			}
			auto repeatGraph = samplerGraph;
			std::erase_if(repeatGraph.links, [&](const Engine::ShaderGraphLink& link) { return link.outputNode == samplerNode; });
			const auto repeated = Engine::ShaderGraphCompiler::Compile(repeatGraph, "repeat.surface.hlsli");
			if (!repeated.Succeeded() || repeated.surfaceHLSL.find("HexTilingData hexGrid_") == std::string::npos ||
				!fixture.WriteGeneratedGraph(repeatGraph, "HexTiling") || !fixture.CompileRasterShaders("HexTiling")) return false;
			if (!fixture.WriteGeneratedGraph(samplerGraph, "Sampler")) {

				return false;
			}

			Engine::ShaderGraphAsset restoredSampler{};
			if (!Engine::FromJson(Engine::ToJson(samplerGraph), restoredSampler)) {

				return false;
			}
			const auto restoredSamplerNode = std::ranges::find_if(
				restoredSampler.nodes, [&](const Engine::ShaderGraphNode& node) { return node.id == samplerNode; });
			if (restoredSamplerNode == restoredSampler.nodes.end() ||
				restoredSamplerNode->sampler.filter != D3D12_FILTER_ANISOTROPIC ||
				restoredSamplerNode->sampler.addressU != D3D12_TEXTURE_ADDRESS_MODE_CLAMP ||
				restoredSamplerNode->sampler.addressV != D3D12_TEXTURE_ADDRESS_MODE_MIRROR ||
				restoredSamplerNode->sampler.maxAnisotropy != 8) {

				return false;
			}
		}

		return true;
	}

	// 頂点変形のVSとMSを確認する
	bool TestShaderGraphVertexTargets(const ShaderGraphCompileFixture& fixture) {

		constexpr std::array vertexTargets{
			Engine::ShaderGraphTarget::Mesh,
			Engine::ShaderGraphTarget::Primitive3D,
			Engine::ShaderGraphTarget::Primitive2D,
		};
		for (const Engine::ShaderGraphTarget target : vertexTargets) {

			Engine::ShaderGraphAsset vertexGraph = Engine::CreateDefaultSurfaceShaderGraph("NEMVertexTargetTest", target);
			const Engine::UUID vertexOutput = Engine::UUID::New();
			vertexGraph.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = vertexOutput,
				.kind = Engine::ShaderGraphNodeKind::VertexOutput,
			});
			vertexGraph.vertexOutputNode = vertexOutput;

			Engine::MaterialParameterValue textureValue{};
			textureValue.value = Engine::AssetID{};
			const Engine::UUID textureParameter = Engine::UUID::New();
			vertexGraph.parameters.emplace_back(Engine::ShaderGraphParameter{
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
			vertexGraph.nodes.insert(vertexGraph.nodes.end(), {
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
			const auto addVertexLink = [&](Engine::UUID source, uint32_t sourceSlot, Engine::UUID destination,
										   uint32_t destinationSlot) {
				vertexGraph.links.emplace_back(Engine::ShaderGraphLink{
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
				Engine::ShaderGraphCompiler::Compile(vertexGraph, "NEMVertexTargetTest.surface.hlsli");
			const bool expectsMeshShader = target != Engine::ShaderGraphTarget::Primitive2D;
			// Primitiveの生成PSも標準描画と同じRingのUV補正を使う
			if (target != Engine::ShaderGraphTarget::Mesh &&
				(vertexOutputResult.opaquePixelHLSL.find("ResolvePrimitivePixelUV") == std::string::npos ||
					vertexOutputResult.transparentPixelHLSL.find("ResolvePrimitivePixelUV") == std::string::npos)) {
				return false;
			}
			const std::string expectedFunction =
				target == Engine::ShaderGraphTarget::Mesh
					? "EvaluateShaderGraphVertex"
					: (target == Engine::ShaderGraphTarget::Primitive3D ? "EvaluatePrimitiveShaderGraphVertex"
																		: "EvaluatePrimitive2DShaderGraphVertex");
			if (!Engine::SupportsShaderGraphVertexOutput(target) || !vertexOutputResult.Succeeded() ||
				vertexOutputResult.vertexHLSL.find(expectedFunction) == std::string::npos ||
				(expectsMeshShader != !vertexOutputResult.meshHLSL.empty()) ||
				!fixture.WriteGeneratedGraph(vertexGraph,
					std::string("Vertex") + std::string(Engine::EnumAdapter<Engine::ShaderGraphTarget>::ToString(target)))) {

				return false;
			}
		}

		// Material Parameterを持つMeshとPrimitiveの両経路を確認する
		return fixture.CompileRasterShaders("Primitive3D") && fixture.CompileRasterShaders("VertexMesh") &&
			fixture.CompileRasterShaders("VertexPrimitive3D");
	}

} // NEMTests
