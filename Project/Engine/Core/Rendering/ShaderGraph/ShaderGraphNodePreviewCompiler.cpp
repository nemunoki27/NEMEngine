#include "ShaderGraphNodePreviewCompiler.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphNodePreviewSource.h"
#include "ShaderGraphStageSource.h"
#include "ShaderGraphSubGraphExpander.h"
#include "ShaderGraphReachability.h"
#include "ShaderGraphNodeRegistry.h"
#include "ShaderGraphPreviewUniforms.h"
#include "ShaderGraphResourceInputs.h"
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>

// c++
#include <algorithm>
#include <format>
#include <unordered_set>

using namespace Engine;
using namespace Engine::ShaderGraphStageSource;

namespace {

	// 参照元の特徴から更新方法を決める
	void CollectDependencies(const ShaderGraphAsset& graph, ShaderGraphNodePreviewCompileOutput& output) {

		for (const ShaderGraphNode& node : graph.nodes) {
			output.usesTime |= node.kind == ShaderGraphNodeKind::Time || node.kind == ShaderGraphNodeKind::CustomFunction;
			output.usesScene |= (ShaderGraphNodeKind::SceneColor <= node.kind && node.kind <= ShaderGraphNodeKind::SceneFlags);
			if (const auto* descriptor = ShaderGraphNodeRegistry::Find(node.kind)) {
				output.usesGeometry |= descriptor->preview3D;
			}
		}
	}
}

bool ShaderGraphNodePreviewCompileOutput::Succeeded() const {

	return !pixelHLSL.empty() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const ShaderGraphDiagnostic& value) {
		return value.severity == ShaderGraphDiagnosticSeverity::Error;
	});
}

std::string Engine::BuildShaderGraphNodePreviewPipelineKey(const ShaderGraphNodePreviewCompileOutput& output) {

	PipelineStaticSamplerOverrideSet samplers{};
	for (const ShaderGraphSamplerBinding& binding : output.samplers) {
		samplers.byName.emplace(binding.shaderName, binding.settings);
	}
	uint64_t metadata = 14695981039346656037ull;
	for (const ShaderParameterMetadata& parameter : output.parameters) {
		metadata = Algorithm::MixHashString(metadata, parameter.shaderName);
		metadata = Algorithm::MixHashString(metadata, parameter.displayName);
		metadata = Algorithm::MixHash(metadata, parameter.id.value);
		metadata = Algorithm::MixHash(metadata, static_cast<uint64_t>(parameter.semantic));
		metadata = Algorithm::MixHash(metadata, parameter.isColor);
		metadata = Algorithm::MixHash(metadata, parameter.isTexture);
		metadata = Algorithm::MixHash(metadata, parameter.exposed);
	}
	return output.pixelHLSL + std::format("\n// sampler {:016x} metadata {:016x}\n",
		HashPipelineStaticSamplerOverrides(&samplers), metadata);
}

ShaderGraphNodePreviewCompileOutput ShaderGraphNodePreviewCompiler::Compile(const ShaderGraphAsset& graph, Engine::UUID nodeID,
	uint32_t outputSlot, const ShaderGraphNodePreviewOptions& options, const ShaderGraphAssetResolver& resolver) {

	ShaderGraphNodePreviewCompileOutput result{};
	ShaderGraphCompileOutput compiled{};
	const auto requested = std::find_if(graph.nodes.begin(), graph.nodes.end(),
		[&](const ShaderGraphNode& node) { return node.id == nodeID; });
	if (requested == graph.nodes.end() || (requested->kind != ShaderGraphNodeKind::SubGraph &&
		GetShaderGraphOutputCount(*requested) <= outputSlot)) {
		result.diagnostics.emplace_back(ShaderGraphDiagnostic{
			.severity = ShaderGraphDiagnosticSeverity::Error,
			.node = nodeID,
			.port = outputSlot,
			.message = "プレビュー対象のノードまたは出力ピンが不正です",
		});
		return result;
	}
	ShaderGraphAsset reachable = graph;
	ShaderGraphReachability::Retain(reachable, nodeID);

	// SubGraphの出力も展開後の端点へ解決する
	Engine::UUID sinkID{UINT64_MAX};
	while (std::any_of(reachable.nodes.begin(), reachable.nodes.end(),
		[&](const ShaderGraphNode& node) { return node.id == sinkID; }) ||
		std::any_of(reachable.links.begin(), reachable.links.end(),
			[&](const ShaderGraphLink& value) { return value.id == sinkID; })) --sinkID.value;
	ShaderGraphNode sink{};
	sink.id = sinkID;
	sink.kind = ShaderGraphNodeKind::UnlitOutput;
	reachable.nodes.emplace_back(sink);
	ShaderGraphLink link{};
	link.id = sinkID;
	link.outputNode = nodeID;
	link.outputSlot = outputSlot;
	link.inputNode = sinkID;
	reachable.links.emplace_back(link);
	reachable.outputNode = sinkID;
	reachable.vertexOutputNode = {};
	if (!ShaderGraphSubGraphExpander::Expand(reachable, resolver, compiled.diagnostics, true)) {
		result.diagnostics = std::move(compiled.diagnostics);
		return result;
	}
	ShaderGraphResourceInputs::Normalize(reachable);
	ShaderGraphAsset validation = reachable;
	validation.domain = ShaderGraphDomain::Surface;
	compiled.ir = ShaderGraphIRBuilder::Build(validation);
	if (!compiled.ir.Succeeded()) {
		result.diagnostics = std::move(compiled.ir.diagnostics);
		return result;
	}
	const auto endpoint = std::find_if(reachable.links.begin(), reachable.links.end(),
		[&](const ShaderGraphLink& value) { return value.inputNode == sinkID; });
	if (endpoint != reachable.links.end()) {
		nodeID = endpoint->outputNode;
		outputSlot = endpoint->outputSlot;
	}
	ShaderGraphReachability::Retain(reachable, nodeID);
	CollectDependencies(reachable, result);
	ShaderGraphPreviewUniforms::Lift(reachable);
	BuildSamplerBindings(reachable, compiled);
	ShaderGraphExpressionCompiler compiler(reachable, compiled);
	const ShaderGraphExpression expression = compiler.EmitNode(nodeID, outputSlot);
	result.valueType = expression.type;
	if (expression.type == ShaderGraphValueType::Invalid && compiled.Succeeded()) {
		compiler.AddDiagnostic(nodeID, "この出力の評価式を生成できません");
	}
	if (compiled.Succeeded() && expression.type != ShaderGraphValueType::Invalid) {
		result.pixelHLSL = ShaderGraphNodePreviewSource::Build(reachable, compiler, expression, options);
	}
	BuildParameterBindings(reachable, compiled);
	result.parameters = std::move(compiled.parameters);
	result.defaultParameters = std::move(compiled.defaultParameters);
	result.samplers = std::move(compiled.samplers);
	result.diagnostics = std::move(compiled.diagnostics);
	return result;
}
