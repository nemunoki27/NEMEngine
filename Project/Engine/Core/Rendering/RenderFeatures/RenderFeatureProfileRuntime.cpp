#include "RenderFeatureProfileRuntime.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace {

	constexpr std::string_view kDefaultOutputName = "Color";

	std::string_view ResolveOutputName(
		const Engine::RenderFeatureOutputReference& reference) {

		return reference.output.empty() ?
			kDefaultOutputName : std::string_view(reference.output);
	}

	bool HasOutput(const Engine::RenderFeaturePassSettings& pass,
		std::string_view outputName) {

		if (pass.outputs.empty()) {
			return outputName == kDefaultOutputName;
		}
		return std::ranges::any_of(pass.outputs,
			[outputName](const Engine::RenderFeatureOutputSettings& output) {

				return output.name == outputName;
			});
	}
}

//============================================================================
//	RenderFeatureProfileRuntime classMethods
//============================================================================
void Engine::RenderFeatureProfileRuntime::Rebuild(
	const RenderFeatureProfileAsset& profile) {

	profile_ = profile;
	ApplyRenderFeatureHierarchy(profile_);
	diagnostic_.clear();
	ValidateProfile();
}

Engine::RenderFeatureExecutionPlan
Engine::RenderFeatureProfileRuntime::BuildPlan(
	RenderFeatureAnchor anchor) const {

	RenderFeatureExecutionPlan plan{};
	if (!diagnostic_.empty()) {
		plan.diagnostic = diagnostic_;
		return plan;
	}
	std::unordered_map<uint64_t, const RenderFeaturePassSettings*>
		activePasses{};
	std::unordered_map<uint64_t, RenderFeatureOutputReference>
		sources{};
	RenderFeatureOutputReference previous{};

	// 同じAnchor内の省略入力は直前Passの主出力へ接続する
	for (const RenderFeaturePassSettings& pass : profile_.passes) {

		if (!pass.enabled || !pass.material || pass.anchor != anchor) {
			continue;
		}
		if (!pass.id ||
			!activePasses.emplace(pass.id.value, &pass).second) {

			plan.diagnostic = "RenderFeatureのPass IDが重複しています";
			return plan;
		}
		switch (pass.sourceKind) {
		case RenderFeatureSourceKind::PreviousPass:
			sources[pass.id.value] = previous;
			break;
		case RenderFeatureSourceKind::SceneColor:
			sources[pass.id.value] = {};
			break;
		case RenderFeatureSourceKind::PassOutput:
			sources[pass.id.value] = pass.source;
			break;
		}
		previous = RenderFeatureOutputReference{
			.pass = pass.id,
			.output = pass.outputs.empty() ?
				"Color" : pass.outputs.front().name,
		};
		if (pass.sceneColorOutput) {
			if (plan.sceneColorOutput.pass) {
				plan.diagnostic =
					"同じ実行位置にSceneColor出力が複数あります";
				return plan;
			}
			plan.sceneColorOutput = previous;
		}
	}

	std::unordered_map<uint64_t, uint8_t> states{};
	std::function<bool(const RenderFeaturePassSettings&)> visit =
		[&](const RenderFeaturePassSettings& pass) {

		uint8_t& state = states[pass.id.value];
		if (state == 2u) {
			return true;
		}
		if (state == 1u) {
			plan.diagnostic = "RenderFeatureグラフに循環参照があります";
			return false;
		}
		state = 1u;

		std::unordered_set<uint64_t> dependencies{};
		const RenderFeatureOutputReference source = sources[pass.id.value];
		if (source.pass) {
			dependencies.insert(source.pass.value);
		}
		for (const auto& [name, input] : pass.passInputs) {

			(void)name;
			if (input.pass) {
				dependencies.insert(input.pass.value);
			}
		}

		for (uint64_t dependencyID : dependencies) {

			const auto current = activePasses.find(dependencyID);
			if (current != activePasses.end()) {
				if (!visit(*current->second)) {
					return false;
				}
				continue;
			}

			const auto external = std::find_if(
				profile_.passes.begin(), profile_.passes.end(),
				[dependencyID](const RenderFeaturePassSettings& candidate) {

					return candidate.id.value == dependencyID;
				});
			if (external == profile_.passes.end() || !external->enabled ||
				GetRenderFeatureAnchorOrder(anchor) <=
				GetRenderFeatureAnchorOrder(external->anchor)) {

				plan.diagnostic =
					"無効または後段のRenderFeatureを参照しています";
				return false;
			}
		}

		state = 2u;
		plan.nodes.emplace_back(RenderFeaturePlanNode{
			.pass = &pass,
			.source = source,
		});
		return true;
	};

	// 出力未接続のPassも副作用を持てるためすべて実行計画へ含める
	for (const RenderFeaturePassSettings& pass : profile_.passes) {

		if (!pass.enabled || !pass.material || pass.anchor != anchor) {
			continue;
		}
		if (!visit(pass)) {
			plan.nodes.clear();
			return plan;
		}
	}
	return plan;
}

bool Engine::RenderFeatureProfileRuntime::ValidateProfile() {

	std::unordered_map<uint64_t, const RenderFeaturePassSettings*> passes{};
	std::unordered_set<std::string> passNames{};
	std::unordered_map<uint32_t, uint32_t> sceneColorCounts{};

	for (const RenderFeaturePassSettings& pass : profile_.passes) {

		if (!pass.id || !passes.emplace(pass.id.value, &pass).second) {
			diagnostic_ = "RenderFeatureのPass IDが重複または未設定です";
			return false;
		}
		if (pass.name.empty() || !passNames.emplace(pass.name).second) {
			diagnostic_ = "RenderFeatureのPass名が重複または未設定です";
			return false;
		}

		std::unordered_set<std::string> outputNames{};
		std::unordered_set<std::string> outputResources{};
		const std::vector<RenderFeatureOutputSettings> defaultOutputs =
			pass.outputs.empty() ?
			std::vector<RenderFeatureOutputSettings>{
				RenderFeatureOutputSettings{} } : pass.outputs;
		for (const RenderFeatureOutputSettings& output : defaultOutputs) {

			if (output.name.empty() || output.shaderResource.empty() ||
				output.widthScale <= 0.0f || output.heightScale <= 0.0f ||
				!outputNames.emplace(output.name).second ||
				!outputResources.emplace(output.shaderResource).second) {

				diagnostic_ = "RenderFeatureの出力設定が不正です";
				return false;
			}
		}
		if (!pass.sceneColorOutput) {
			if (pass.targetMask == 0u) {
				continue;
			}
		}

		const RenderFeatureOutputSettings& output = defaultOutputs.front();
		if (output.format != RenderFeatureTextureFormat::Inherit ||
			output.widthScale != 1.0f || output.heightScale != 1.0f) {

			diagnostic_ =
				"SceneColor出力と対象マスクは継承形式かつ等倍が必要です";
			return false;
		}
		if (!pass.sceneColorOutput) {
			continue;
		}
		uint32_t& count = sceneColorCounts[
			GetRenderFeatureAnchorOrder(pass.anchor)];
		if (++count > 1u) {
			diagnostic_ = "同じ実行位置にSceneColor出力が複数あります";
			return false;
		}
	}

	const auto validateReference = [&](const RenderFeaturePassSettings& owner,
		const RenderFeatureOutputReference& reference) {

		if (!reference.pass) {
			return true;
		}
		const auto found = passes.find(reference.pass.value);
		if (found == passes.end() || !found->second->enabled ||
			!HasOutput(*found->second, ResolveOutputName(reference))) {

			diagnostic_ = "RenderFeatureが存在しない出力を参照しています";
			return false;
		}
		if (GetRenderFeatureAnchorOrder(found->second->anchor) >
			GetRenderFeatureAnchorOrder(owner.anchor)) {

			diagnostic_ = "RenderFeatureが後段のPassを参照しています";
			return false;
		}
		return true;
	};

	for (const RenderFeaturePassSettings& pass : profile_.passes) {

		if (!pass.enabled) {
			continue;
		}
		if (pass.sourceKind == RenderFeatureSourceKind::PassOutput &&
			!pass.source.pass) {

			diagnostic_ = "RenderFeatureの主入力Passが未設定です";
			return false;
		}
		if (pass.sourceKind == RenderFeatureSourceKind::PassOutput &&
			!validateReference(pass, pass.source)) {
			return false;
		}
		for (const auto& [resourceName, reference] : pass.passInputs) {

			if (resourceName.empty() || !validateReference(pass, reference)) {
				if (diagnostic_.empty()) {
					diagnostic_ = "RenderFeatureの入力名が未設定です";
				}
				return false;
			}
		}
	}
	return true;
}
