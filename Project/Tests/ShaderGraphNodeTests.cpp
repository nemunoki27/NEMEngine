#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Decals/DecalGraphProgram.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphSourceUtility.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>

// c++
#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace NEMTests {

	// 演算ノードの既定値と保存値を確認する
	bool TestShaderGraphNodeDefaults() {

		// Time出力とPBRの発光ピンを保存後の投影式でも識別する
		for (const auto target : {Engine::ShaderGraphTarget::Mesh, Engine::ShaderGraphTarget::Primitive3D}) {
			auto graph = Engine::CreateDefaultSurfaceShaderGraph("PaintTime", target);
			const auto time = Engine::UUID::New();
			graph.nodes.push_back({.id = time, .kind = Engine::ShaderGraphNodeKind::Time});
			std::erase_if(graph.links, [&](const auto& link) { return link.inputNode == graph.outputNode && link.inputSlot == 5; });
			graph.links.push_back({.id = Engine::UUID::New(), .outputNode = time, .outputSlot = 1, .inputNode = graph.outputNode, .inputSlot = 5});
			const auto compiled = Engine::ShaderGraphCompiler::Compile(graph, "surface.hlsli");
			if (!compiled.Succeeded() || !compiled.paintGraph) return false;
			Engine::MaterialAsset material;
			material.shaderGraph = Engine::AssetID::New();
			material.paintGraph = compiled.paintGraph;
			Engine::MaterialAsset loaded;
			if (!Engine::FromJson(Engine::ToJson(material), loaded) || !loaded.paintGraph ||
				!Engine::MaterialRenderPolicy::GetMaterialPaintUnsupportedReason(loaded).empty()) return false;
			loaded.shaderGraph = {};
			if (!Engine::MaterialRenderPolicy::GetMaterialPaintUnsupportedReason(loaded).empty()) return false;
			const auto has = [&](Engine::DecalGraphOp op, float port) {
				return std::ranges::any_of(loaded.paintGraph->instructions, [&](const auto& i) { return i.op == op && i.value.x == port; });
			};
			if (!has(Engine::DecalGraphOp::Time, 1) || !has(Engine::DecalGraphOp::Output, 5)) return false;
			graph.surfaceMode = Engine::ShaderGraphSurfaceMode::Transparent;
			if (Engine::ShaderGraphCompiler::Compile(graph, "surface.hlsli").paintGraph) return false;
		}
		// 非公開値とGlobal値も保存された既定値を持つ
		for (const auto scope : {Engine::ShaderGraphParameterScope::PerMaterial,
			Engine::ShaderGraphParameterScope::PerInstance, Engine::ShaderGraphParameterScope::Global}) {
			for (const bool exposed : {false, true}) {
				auto parameterGraph = Engine::CreateDefaultSurfaceShaderGraph("ParameterDefault");
				const auto parameterID = Engine::UUID::New(), nodeID = Engine::UUID::New();
				parameterGraph.parameters.emplace_back(Engine::ShaderGraphParameter{
					.id = parameterID, .name = "Tint", .type = Engine::ShaderGraphValueType::Float4,
					.defaultValue = {Engine::Vector4(.2f, .4f, .6f, .75f)}, .scope = scope, .exposed = exposed,
				});
				parameterGraph.nodes.emplace_back(Engine::ShaderGraphNode{
					.id = nodeID, .kind = Engine::ShaderGraphNodeKind::Parameter, .parameterID = parameterID,
				});
				std::erase_if(parameterGraph.links, [&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == parameterGraph.outputNode && link.inputSlot == 0;
				});
				parameterGraph.links.emplace_back(Engine::ShaderGraphLink{
					.id = Engine::UUID::New(), .outputNode = nodeID, .inputNode = parameterGraph.outputNode,
				});
				const auto compiled = Engine::ShaderGraphCompiler::Compile(parameterGraph, "surface.hlsli");
				const auto* initial = compiled.defaultParameters.Find(Engine::MaterialParameterID::FromUUID(parameterID));
				if (!compiled.Succeeded() || !initial || !std::holds_alternative<Engine::Vector4>(initial->value)) return false;
				const auto& value = std::get<Engine::Vector4>(initial->value);
				if (value.x != .2f || value.y != .4f || value.z != .6f || value.w != .75f) return false;
				const auto parameterIDValue = Engine::MaterialParameterID::FromUUID(parameterID);
				const auto field = std::find_if(compiled.parameters.begin(), compiled.parameters.end(),
					[&](const Engine::ShaderParameterMetadata& metadata) { return metadata.id == parameterIDValue; });
				const bool canOverride = exposed && scope != Engine::ShaderGraphParameterScope::Global;
				if (field == compiled.parameters.end() || field->exposed != canOverride) return false;

				// 公開値の編集を保ち、内部値は再コンパイル後の既定値へ更新する
				Engine::ShaderGraphArtifact artifact;
				artifact.compileOutput = compiled;
				auto material = Engine::ShaderGraphArtifactCache::CreateMaterial(parameterGraph, {71, 40});
				const Engine::Vector4 authored(.9f, .8f, .7f, .6f);
				material.parameters.Set(parameterIDValue, "Tint", Engine::MaterialParameterSemantic::None, {authored});
				Engine::ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
				const auto* applied = material.parameters.Find(parameterIDValue);
				const Engine::Vector4 expected = canOverride ? authored : value;
				if (!applied || !std::holds_alternative<Engine::Vector4>(applied->value) ||
					std::get<Engine::Vector4>(applied->value) != expected) return false;
				const Engine::Vector4 changed(.3f, .6f, .9f, .5f);
				artifact.compileOutput.defaultParameters.Set(parameterIDValue, "Tint",
					Engine::MaterialParameterSemantic::None, {changed});
				Engine::ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
				applied = material.parameters.Find(parameterIDValue);
				if (!applied || std::get<Engine::Vector4>(applied->value) != (canOverride ? authored : changed)) return false;
			}
		}

		// 整数の境界値と実数の変換失敗を確認する
		const std::array<std::pair<Engine::MaterialParameterValue, std::string_view>, 8> integerCases{{
			{{int32_t(2147483647)}, "2147483647"},
			{{std::numeric_limits<int32_t>::min()}, "-2147483648"},
			{{int32_t(16777217)}, "16777217"},
			{{std::numeric_limits<uint32_t>::max()}, "-1"},
			{{3.75f}, "3"},
			{{2147483648.0f}, "0"},
			{{std::numeric_limits<float>::infinity()}, "0"},
			{{std::numeric_limits<float>::quiet_NaN()}, "0"},
		}};
		for (const auto& [value, expected] : integerCases) {
			if (Engine::ShaderGraphSourceUtility::MakeLiteral(value, Engine::ShaderGraphValueType::Integer) != expected) {
				return false;
			}
		}

		// 描画方式ごとのScene Textureの入力名を確認する
		constexpr std::array sceneKinds{
			Engine::ShaderGraphNodeKind::SceneColor,
			Engine::ShaderGraphNodeKind::SceneMaterial,
			Engine::ShaderGraphNodeKind::SceneEmissive,
		};
		for (bool postProcess : {false, true}) {
			for (const Engine::ShaderGraphNodeKind kind : sceneKinds) {
				Engine::ShaderGraphAsset sceneGraph = postProcess ?
					Engine::CreateDefaultPostProcessShaderGraph("SceneInput") :
					Engine::CreateDefaultSurfaceShaderGraph("SceneInput");
				sceneGraph.surfaceMode = Engine::ShaderGraphSurfaceMode::Transparent;
				std::erase_if(sceneGraph.links, [&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == sceneGraph.outputNode && link.inputSlot == 0;
				});
				const Engine::UUID nodeID = Engine::UUID::New();
				sceneGraph.nodes.emplace_back(Engine::ShaderGraphNode{.id = nodeID, .kind = kind});
				sceneGraph.links.emplace_back(Engine::ShaderGraphLink{
					.id = Engine::UUID::New(), .outputNode = nodeID, .inputNode = sceneGraph.outputNode,
				});
				const Engine::ShaderGraphCompileOutput output =
					Engine::ShaderGraphCompiler::Compile(sceneGraph, "SceneInput.surface.hlsli");
				const std::string& source = postProcess ? output.computeHLSL : output.surfaceHLSL;
				const char* textureName = kind == Engine::ShaderGraphNodeKind::SceneMaterial ? "gShaderGraphSceneMaterial" :
					kind == Engine::ShaderGraphNodeKind::SceneEmissive ? "gShaderGraphSceneEmissive" :
					postProcess ? "gSourceColor" : "gShaderGraphSceneColor";
				if (!output.Succeeded() || source.find(std::string(textureName) + ".SampleLevel(") == std::string::npos) {
					return false;
				}
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
			Engine::ShaderGraphNodeKind::Clamp,
			Engine::ShaderGraphNodeKind::Split,
			Engine::ShaderGraphNodeKind::Combine,
			Engine::ShaderGraphNodeKind::Dither,
		};
		for (const Engine::ShaderGraphNodeKind kind : additionalNodeKinds) {

			Engine::ShaderGraphAsset nodeGraph = Engine::CreateDefaultSurfaceShaderGraph("NEMNodeTest");
			for (auto it = nodeGraph.links.begin(); it != nodeGraph.links.end();) {

				if (it->inputNode == nodeGraph.outputNode && it->inputSlot == 0) {

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
				Engine::ShaderGraphCompiler::Compile(nodeGraph, "NEMNodeTest.surface.hlsli");
			if (!nodeOutput.Succeeded()) {
				return false;
			}
			if (kind == Engine::ShaderGraphNodeKind::Clamp &&
				nodeOutput.surfaceHLSL.find("clamp(0.0f, 0.0f, 1.0f)") == std::string::npos) {

				return false;
			}

			Engine::ShaderGraphAsset restored{};
			if (!Engine::FromJson(Engine::ToJson(nodeGraph), restored)) {
				return false;
			}
			const Engine::ShaderGraphNode& restoredNode = restored.nodes.back();
			if (restoredNode.kind != kind || restoredNode.previewExpanded) {

				return false;
			}
		}

		return true;
	}

	// 時刻ノードの全出力を確認する
	bool TestShaderGraphTimeOutputs() {

		constexpr std::array timeExpressions{
			"shaderGraphTime",
			"sin(shaderGraphTime)",
			"cos(shaderGraphTime)",
			"shaderGraphDeltaTime",
			"shaderGraphSmoothDeltaTime",
		};
		for (uint32_t outputSlot = 0; outputSlot < timeExpressions.size(); ++outputSlot) {

			Engine::ShaderGraphAsset timeGraph = Engine::CreateDefaultSurfaceShaderGraph("NEMTimeTest");
			std::erase_if(timeGraph.links, [&](const Engine::ShaderGraphLink& link) {
				return link.inputNode == timeGraph.outputNode && link.inputSlot == 0;
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
				Engine::ShaderGraphCompiler::Compile(timeGraph, "NEMTimeTest.surface.hlsli");
			const std::string variable = Engine::ShaderGraphSourceUtility::MakeNodeVariable("value" + std::to_string(outputSlot), timeNodeID);
			if (!timeOutput.Succeeded() || timeOutput.surfaceHLSL.find("const float " + variable + " = " +
				timeExpressions[outputSlot] + ";") == std::string::npos ||
				timeOutput.surfaceHLSL.find("(" + variable + ").xxxx") == std::string::npos) {

				return false;
			}
		}

		return true;
	}

} // NEMTests
