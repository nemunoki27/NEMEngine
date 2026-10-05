#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>

// c++
#include <algorithm>
#include <array>

namespace NEMTests {

	// 演算ノードの既定値と保存値を確認する
	bool TestShaderGraphNodeDefaults() {

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
			"(shaderGraphTime).xxxx",
			"(sin(shaderGraphTime)).xxxx",
			"(cos(shaderGraphTime)).xxxx",
			"(shaderGraphDeltaTime).xxxx",
			"(shaderGraphSmoothDeltaTime).xxxx",
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
			if (!timeOutput.Succeeded() || timeOutput.surfaceHLSL.find(timeExpressions[outputSlot]) == std::string::npos) {

				return false;
			}
		}

		return true;
	}

} // NEMTests
