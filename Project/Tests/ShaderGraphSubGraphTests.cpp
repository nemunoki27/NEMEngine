#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>

// c++
#include <algorithm>

namespace NEMTests {

	// SubGraph展開と重複IDの拒否を確認する
	bool TestShaderGraphSubGraphs(Engine::ShaderGraphAsset& graph) {

		// Sub Graphは公開パラメータを入力、参照先Outputを出力として展開する
		{
			Engine::ShaderGraphAsset child = Engine::CreateDefaultSurfaceShaderGraph("NEMSubGraph");
			std::erase_if(child.links,
				[&](const Engine::ShaderGraphLink& link) { return link.inputNode == child.outputNode && link.inputSlot == 0; });
			const Engine::UUID parameterID = Engine::UUID::New();
			child.parameters.emplace_back(Engine::ShaderGraphParameter{
				.id = parameterID,
				.name = "Color",
				.type = Engine::ShaderGraphValueType::Color,
				.defaultValue =
					Engine::MaterialParameterValue{
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

			Engine::ShaderGraphAsset parent = Engine::CreateDefaultSurfaceShaderGraph("NEMSubGraphParent");
			const Engine::UUID colorNode = parent.links.front().outputNode;
			std::erase_if(parent.links, [&](const Engine::ShaderGraphLink& link) {
				return link.inputNode == parent.outputNode && link.inputSlot == 0;
			});
			const Engine::UUID subGraphNode = Engine::UUID::New();
			const Engine::AssetID subGraphID{10, 20};
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
			const Engine::ShaderGraphCompileOutput subGraphOutput = Engine::ShaderGraphCompiler::Compile(
				parent, "NEMSubGraph.surface.hlsli", [&](Engine::AssetID id, Engine::ShaderGraphAsset& outGraph) {
					if (id != subGraphID) {
						return false;
					}
					outGraph = child;
					return true;
				});
			if (!subGraphOutput.Succeeded() || subGraphOutput.surfaceHLSL.find("NEMSubGraph") != std::string::npos) {
				return false;
			}
		}

		graph.nodes[1].id = graph.nodes[0].id;
		const Engine::ShaderGraphCompileOutput invalid = Engine::ShaderGraphCompiler::Compile(graph, "NEMTest.surface.hlsli");
		return !invalid.Succeeded() && !invalid.diagnostics.empty();
	}

} // NEMTests
