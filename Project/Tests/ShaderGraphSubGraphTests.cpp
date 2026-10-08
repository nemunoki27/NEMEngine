#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphSettingsImporter.h>

// c++
#include <algorithm>

namespace {

	using namespace Engine;
	using Engine::UUID;

	// SubGraphの指定出力を親の入力へ接続する
	void ConnectSubGraph(ShaderGraphAsset& parent, AssetID childID, uint32_t inputSlot) {

		std::erase_if(parent.links, [&](const ShaderGraphLink& link) {
			return link.inputNode == parent.outputNode && link.inputSlot == inputSlot;
		});
		const UUID nodeID = UUID::New();
		parent.nodes.emplace_back(ShaderGraphNode{
			.id = nodeID,
			.kind = ShaderGraphNodeKind::SubGraph,
			.subGraph = childID,
		});
		parent.links.emplace_back(ShaderGraphLink{
			.id = UUID::New(),
			.outputNode = nodeID,
			.outputSlot = 2,
			.inputNode = parent.outputNode,
			.inputSlot = inputSlot,
		});
	}

	// 複数配置と入れ子のKeywordの定義と初期値を確認する
	bool TestSubGraphKeywords() {

		// 下位32bitが同じIDでもShader上の名前を区別する
		auto collisionGraph = CreateDefaultSurfaceShaderGraph("KeywordIDCollision");
		for (uint32_t index = 0; index < 2; ++index) {
			const UUID keywordID{(static_cast<uint64_t>(index + 1) << 32) | 1ull};
			const UUID nodeID = UUID::New();
			collisionGraph.keywords.emplace_back(ShaderGraphKeyword{
				.id = keywordID,
				.name = "Collision",
				.referenceName = "COLLISION",
				.defaultIndex = 1,
				.runtimeToggle = true,
			});
			collisionGraph.nodes.emplace_back(ShaderGraphNode{
				.id = nodeID,
				.kind = ShaderGraphNodeKind::Keyword,
				.keywordID = keywordID,
			});
			std::erase_if(collisionGraph.links, [&](const ShaderGraphLink& link) {
				return link.inputNode == collisionGraph.outputNode && link.inputSlot == index + 2;
			});
			collisionGraph.links.emplace_back(ShaderGraphLink{
				.id = UUID::New(),
				.outputNode = nodeID,
				.inputNode = collisionGraph.outputNode,
				.inputSlot = index + 2,
			});
		}
		const auto collisionOutput = ShaderGraphCompiler::Compile(collisionGraph, "KeywordIDCollision.surface.hlsli");
		if (!collisionOutput.Succeeded() || collisionOutput.parameters.size() != 2 ||
			collisionOutput.parameters[0].shaderName == collisionOutput.parameters[1].shaderName) {
			return false;
		}
		// ParameterとKeywordも同じMaterial IDを共有しない
		auto sharedIDGraph = collisionGraph;
		sharedIDGraph.parameters.emplace_back(ShaderGraphParameter{
			.id = collisionGraph.keywords.front().id,
			.name = "SharedID",
		});
		const auto sharedIDOutput = ShaderGraphCompiler::Compile(sharedIDGraph, "SharedID.surface.hlsli");
		if (sharedIDOutput.Succeeded() || sharedIDOutput.diagnostics.empty() || !sharedIDOutput.surfaceHLSL.empty()) {
			return false;
		}

		const AssetID childID{20, 21}, wrapperID{20, 22}, parentID{20, 23}, materialID{20, 24};
		for (uint32_t runtime = 0; runtime < 2; ++runtime) {
			for (uint32_t enumeration = 0; enumeration < 2; ++enumeration) {
				for (uint32_t nested = 0; nested < 2; ++nested) {
					auto child = CreateDefaultSurfaceShaderGraph("KeywordChild");
					std::erase_if(child.links, [&](const ShaderGraphLink& link) {
						return link.inputNode == child.outputNode && link.inputSlot == 2;
					});
					const UUID keywordID = UUID::New(), keywordNodeID = UUID::New();
					child.keywords.emplace_back(ShaderGraphKeyword{
						.id = keywordID,
						.name = "Feature",
						.referenceName = "FEATURE",
						.type = enumeration ? ShaderGraphKeywordType::Enum : ShaderGraphKeywordType::Boolean,
						.entries = {"Off", "On"},
						.defaultIndex = 1,
						.runtimeToggle = runtime != 0,
					});
					child.nodes.emplace_back(ShaderGraphNode{
						.id = keywordNodeID,
						.kind = ShaderGraphNodeKind::Keyword,
						.keywordID = keywordID,
					});
					child.links.emplace_back(ShaderGraphLink{
						.id = UUID::New(),
						.outputNode = keywordNodeID,
						.inputNode = child.outputNode,
						.inputSlot = 2,
					});
					auto wrapper = CreateDefaultSurfaceShaderGraph("KeywordWrapper");
					ConnectSubGraph(wrapper, childID, 2);
					auto parent = CreateDefaultSurfaceShaderGraph("KeywordParent");
					ConnectSubGraph(parent, nested ? wrapperID : childID, 2);
					ConnectSubGraph(parent, nested ? wrapperID : childID, 3);
					const auto resolve = [&](AssetID id, ShaderGraphAsset& graph) {
						if (id != childID && id != wrapperID) {
							return false;
						}
						graph = id == childID ? child : wrapper;
						return true;
					};
					const auto output = ShaderGraphCompiler::Compile(parent, "Keyword.surface.hlsli", resolve);
					const auto repeated = ShaderGraphCompiler::Compile(parent, "Keyword.surface.hlsli", resolve);
					if (!output.Succeeded() || !repeated.Succeeded() || output.surfaceHLSL != repeated.surfaceHLSL ||
						output.parameters.size() != (runtime ? 2 : 0) ||
						output.defaultParameters.GetRecords().size() != (runtime ? 2 : 0)) {
						return false;
					}
					if (!runtime) {
						continue;
					}
					if (output.parameters[0].id == output.parameters[1].id ||
						output.parameters[0].id == MaterialParameterID::FromUUID(keywordID) ||
						output.parameters[0].id != repeated.parameters[0].id ||
						output.parameters[1].id != repeated.parameters[1].id ||
						output.parameters[0].shaderName == output.parameters[1].shaderName) {
						return false;
					}

					// 生成時は初期値を追加し、再適用では編集値を保つ
					ShaderGraphArtifact artifact;
					artifact.compileOutput = output;
					auto material = ShaderGraphArtifactCache::CreateMaterial(parent, parentID);
					ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
					for (const auto& parameter : output.parameters) {
						const auto* value = material.parameters.Find(parameter.id);
						if (!value || (enumeration ? std::get<int32_t>(value->value) != 1 : !std::get<bool>(value->value))) {
							return false;
						}
					}

					// 子の初期値を含むMaterialも設定取り込みで維持する
					const auto importResolver = [&](AssetID id, AssetType type, nlohmann::json& data) {
						if (type == AssetType::Material && id == materialID) {
							data = ToJson(material);
							return true;
						}
						if (type != AssetType::ShaderGraph) {
							return false;
						}
						ShaderGraphAsset resolved;
						if (id == parentID) {
							resolved = parent;
						} else if (!resolve(id, resolved)) {
							return false;
						}
						data = ToJson(resolved);
						return true;
					};
					ShaderGraphAsset imported;
					std::string error;
					if (!ShaderGraphSettingsImporter::Import(parent, materialID, AssetType::Material,
						importResolver, imported, error) || ToJson(imported) != ToJson(parent)) {
						return false;
					}
					MaterialParameterValue changed;
					changed.value = enumeration ? decltype(changed.value){int32_t{0}} : decltype(changed.value){false};
					material.parameters.Set(output.parameters[0].id, "Feature", MaterialParameterSemantic::None, changed);
					ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
					const auto* maintained = material.parameters.Find(output.parameters[0].id);
					if (!maintained || (enumeration ? std::get<int32_t>(maintained->value) != 0 : std::get<bool>(maintained->value))) {
						return false;
					}
					const auto before = ToJson(imported);
					if (ShaderGraphSettingsImporter::Import(parent, materialID, AssetType::Material,
						importResolver, imported, error) || error.empty() || ToJson(imported) != before) {
						return false;
					}
				}
			}
		}
		return true;
	}
}

namespace NEMTests {

	// SubGraph展開と重複IDの拒否を確認する
	bool TestShaderGraphSubGraphs(Engine::ShaderGraphAsset& graph) {

		if (!TestSubGraphKeywords()) {
			return false;
		}

		// 未使用の公開値も未設定IDと重複IDを拒否する
		for (uint32_t kind = 0; kind < 2; ++kind) {
			for (uint32_t invalidKind = 0; invalidKind < 2; ++invalidKind) {
				Engine::ShaderGraphAsset invalidGraph = Engine::CreateDefaultSurfaceShaderGraph("InvalidPublicID");
				const Engine::UUID id = invalidKind == 0 ? Engine::UUID{} : Engine::UUID::New();
				if (kind == 0) {
					invalidGraph.parameters.emplace_back(Engine::ShaderGraphParameter{.id = id, .name = "First"});
					if (invalidKind == 1) {
						invalidGraph.parameters.emplace_back(Engine::ShaderGraphParameter{.id = id, .name = "Second"});
					}
				} else {
					invalidGraph.keywords.emplace_back(Engine::ShaderGraphKeyword{.id = id, .name = "First"});
					if (invalidKind == 1) {
						invalidGraph.keywords.emplace_back(Engine::ShaderGraphKeyword{.id = id, .name = "Second"});
					}
				}
				const Engine::ShaderGraphCompileOutput invalidOutput =
					Engine::ShaderGraphCompiler::Compile(invalidGraph, "InvalidPublicID.surface.hlsli");
				if (invalidOutput.Succeeded() || invalidOutput.diagnostics.empty() || !invalidOutput.surfaceHLSL.empty()) {
					return false;
				}
			}
		}

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

			// 子の公開値も定数へ置き換える前に不正IDを拒否する
			for (uint32_t kind = 0; kind < 2; ++kind) {
				for (uint32_t invalidKind = 0; invalidKind < 2; ++invalidKind) {
					auto invalidChild = child;
					if (kind == 0) {
						if (invalidKind == 0) {
							invalidChild.parameters.emplace_back(Engine::ShaderGraphParameter{.id = {}, .name = "Unused"});
						} else {
							invalidChild.parameters.emplace_back(invalidChild.parameters.front());
						}
					} else {
						const Engine::UUID id = invalidKind == 0 ? Engine::UUID{} : Engine::UUID::New();
						invalidChild.keywords.emplace_back(Engine::ShaderGraphKeyword{.id = id, .name = "First"});
						if (invalidKind == 1) {
							invalidChild.keywords.emplace_back(Engine::ShaderGraphKeyword{.id = id, .name = "Second"});
						}
					}
					const auto invalidOutput = Engine::ShaderGraphCompiler::Compile(
						parent, "InvalidChildID.surface.hlsli", [&](Engine::AssetID id, Engine::ShaderGraphAsset& result) {
							if (id != subGraphID) {
								return false;
							}
							result = invalidChild;
							return true;
						});
					if (invalidOutput.Succeeded() || invalidOutput.diagnostics.empty() || !invalidOutput.surfaceHLSL.empty()) {
						return false;
					}
				}
			}
		}

		graph.nodes[1].id = graph.nodes[0].id;
		const Engine::ShaderGraphCompileOutput invalid = Engine::ShaderGraphCompiler::Compile(graph, "NEMTest.surface.hlsli");
		return !invalid.Succeeded() && !invalid.diagnostics.empty();
	}

} // NEMTests
