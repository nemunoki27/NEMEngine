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

// c++
#include <algorithm>
#include <array>

namespace NEMTests {

	bool TestShaderGraphShaderDefinitions() {

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
		const std::vector<std::string> effectEntries{"RenderFeatureRayGeneration", "ReflectionMiss", "ReflectionClosestHit"};
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
			std::pair{Engine::ShaderGraphTarget::Trail, "Builtin/Particle/Common/particle.hlsli"},
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
				samplerOutput.surfaceHLSL.find("register(s2)") == std::string::npos ||
				samplerOutput.surfaceHLSL.find(samplerOutput.samplers.front().shaderName) == std::string::npos) {

				return false;
			}
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

		return true;
	}

} // NEMTests
