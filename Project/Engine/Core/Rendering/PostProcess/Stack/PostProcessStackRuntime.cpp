#include "PostProcessStackRuntime.h"

// c++
#include <functional>
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	PostProcessStackRuntime classMethods
//============================================================================
bool Engine::PostProcessStackRuntime::HasEnabledPassesForAnchor(PostProcessAnchor anchor) const {

	for (const auto& pass : passes) {
		if (pass.enabled && pass.anchor == anchor) {
			return true;
		}
	}
	return false;
}

Engine::PostProcessGraphPlan
Engine::PostProcessStackRuntime::BuildGraphPlan(
	PostProcessAnchor anchor) const {

	PostProcessGraphPlan plan{};
	std::unordered_map<uint64_t,
		const PostProcessStackRuntimePass*> activePasses{};
	std::unordered_map<uint64_t, UUID> sourcePasses{};
	UUID previousPass{};

	// 未接続パスは同じAnchor内の直前パスへ接続して編集時の手数を減らす
	for (const PostProcessStackRuntimePass& pass : passes) {
		if (!pass.enabled || !pass.material ||
			pass.anchor != anchor) {
			continue;
		}
		if (!pass.id ||
			!activePasses.emplace(
				pass.id.value, &pass).second) {

			plan.diagnostic =
				"PostProcessパスIDが重複しています";
			return plan;
		}

		sourcePasses[pass.id.value] =
			pass.sourcePass ?
			pass.sourcePass : previousPass;
		previousPass = pass.id;
		if (pass.graphOutput) {
			if (plan.outputPass) {
				plan.diagnostic =
					"PostProcessグラフの最終出力が複数あります";
				return plan;
			}
			plan.outputPass = pass.id;
		}
	}

	if (activePasses.empty()) {
		return plan;
	}
	if (!plan.outputPass) {
		plan.outputPass = previousPass;
	}

	std::unordered_map<uint64_t, uint8_t> states{};
	std::function<bool(UUID)> visit =
		[&](UUID passID) {

		const auto found =
			activePasses.find(passID.value);
		if (found == activePasses.end()) {
			plan.diagnostic =
				"無効または別の実行位置のパスを参照しています";
			return false;
		}

		uint8_t& state = states[passID.value];
		if (state == 2) {
			return true;
		}
		if (state == 1) {
			plan.diagnostic =
				"PostProcessグラフに循環参照があります";
			return false;
		}
		state = 1;

		const PostProcessStackRuntimePass& pass =
			*found->second;
		std::unordered_set<uint64_t> dependencies{};
		const UUID source = sourcePasses[passID.value];
		if (source) {
			dependencies.insert(source.value);
		}
		for (const auto& [name, input] :
			pass.passInputs) {

			(void)name;
			if (input) {
				dependencies.insert(input.value);
			}
		}
		for (const uint64_t dependency :
			dependencies) {

			if (!visit(UUID{ dependency })) {
				return false;
			}
		}

		state = 2;
		plan.nodes.emplace_back(
			PostProcessGraphPlanNode{
				.pass = &pass,
				.sourcePass = source,
			});
		return true;
		};

	if (!visit(plan.outputPass)) {
		plan.nodes.clear();
	}
	return plan;
}
