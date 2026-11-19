#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	// NodeID順にSamplerの配置を決める
	void BuildSamplerBindings(const ShaderGraphAsset& graph, ShaderGraphCompileOutput& output) {

		std::vector<const ShaderGraphNode*> samplerNodes;
		for (const ShaderGraphNode& node : graph.nodes) {
			if (node.kind == ShaderGraphNodeKind::SamplerState) {
				samplerNodes.emplace_back(&node);
			}
		}
		std::sort(samplerNodes.begin(), samplerNodes.end(), [](const ShaderGraphNode* lhs, const ShaderGraphNode* rhs) {
			return lhs->id.value < rhs->id.value;
		});

		// s0は既定サンプラー、s1はレイトレーシングの環境サンプラー
		uint32_t shaderRegister = 2;
		for (const ShaderGraphNode* node : samplerNodes) {
			const auto& settings = node->sampler;
			const bool comparison = D3D12_DECODE_FILTER_REDUCTION(settings.filter) == D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
			const bool validLOD = std::isfinite(settings.mipLODBias) && std::isfinite(settings.minLOD) &&
				std::isfinite(settings.maxLOD) && settings.minLOD <= settings.maxLOD &&
				D3D12_MIP_LOD_BIAS_MIN <= settings.mipLODBias && settings.mipLODBias <= D3D12_MIP_LOD_BIAS_MAX;
			if (comparison || !validLOD) {
				output.diagnostics.emplace_back(ShaderGraphDiagnostic{
					.node = node->id,
					.message = comparison ? "Sampler Stateでは比較用フィルターを使用できません" : "Sampler StateのLOD範囲が不正です",
				});
			}
			// 異なるGraphの同じs番号を命中Shader内で区別する
			PipelineStaticSamplerOverrideSet samplerKey{};
			samplerKey.byName.emplace("", settings);
			const uint64_t hash = HashPipelineStaticSamplerOverrides(&samplerKey);
			const uint32_t hitSpace = 16u + static_cast<uint32_t>(hash ^ (hash >> 32u)) % 0x70000000u;
			output.samplers.emplace_back(ShaderGraphSamplerBinding{
				.node = node->id,
				.shaderName = MakeNodeVariable("gSampler", node->id),
				.shaderRegister = shaderRegister++,
				.hitRegisterSpace = hitSpace,
				.settings = node->sampler,
			});
		}
	}
} // Engine::ShaderGraphStageSource
