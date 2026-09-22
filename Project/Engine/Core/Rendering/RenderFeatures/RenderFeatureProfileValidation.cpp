#include "RenderFeatureProfileValidation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <unordered_map>

namespace {

	void CollectGroupPasses(
		const Engine::RenderFeatureHierarchyItem& group,
		std::vector<Engine::UUID>& outPasses) {

		for (const Engine::RenderFeatureHierarchyItem& child : group.children) {
			if (child.type ==
				Engine::RenderFeatureHierarchyItemType::Pass) {

				outPasses.emplace_back(child.id);
				continue;
			}
			CollectGroupPasses(child, outPasses);
		}
	}
}

namespace Engine::RenderFeatureProfileValidation {

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

bool Engine::RenderFeatureProfileValidation::Validate(const RenderFeatureProfileAsset& profile, std::string& diagnostic) {

	std::unordered_map<uint64_t, const RenderFeaturePassSettings*> passes{};
	std::unordered_set<std::string> passNames{};
	std::unordered_map<uint32_t, uint32_t> sceneColorCounts{};
	std::unordered_set<std::string> groupNames{};
	std::vector<const RenderFeatureHierarchyItem*> isolatedSelections{};
	std::function<bool(const std::vector<RenderFeatureHierarchyItem>&, bool)>
		validateItems = [&](const std::vector<RenderFeatureHierarchyItem>& items,
			bool insideSelection) {

		for (const RenderFeatureHierarchyItem& item : items) {
			const bool group =
				item.type == RenderFeatureHierarchyItemType::Group;
			if (group && (item.name.empty() ||
				!groupNames.emplace(item.name).second)) {

				diagnostic =
					"RenderFeatureのグループ名が重複または未設定です";
				return false;
			}
			const bool selective = item.selection.mode !=
				RenderFeatureSelectionMode::Organization;
			if (selective && insideSelection) {
				diagnostic =
					"選択適用の中へ別の選択適用は配置できません";
				return false;
			}
			if (selective) {
				const RenderFeatureSelectionSettings& selection =
					item.selection;
				if ((selection.renderingLayerMask &
					kRenderingLayerMaskBits) == 0u ||
					(selection.phaseMask &
						kRenderFeatureSelectablePhaseMask) == 0u ||
					(selection.rendererMask &
						RenderFeatureRendererMask::All) == 0u) {

					diagnostic =
						"選択適用の抽出条件が空です";
					return false;
				}
				if (selection.mode ==
					RenderFeatureSelectionMode::IsolatedLayer &&
					(selection.phaseMask & MakeRenderFeaturePhaseMask(
						RenderPhase::Opaque)) != 0u) {

					diagnostic =
						"OpaqueはMaskedSceneColor方式でのみ選択できます";
					return false;
				}
				std::vector<UUID> selectedPasses{};
				if (group) {
					CollectGroupPasses(item, selectedPasses);
				} else {
					selectedPasses.emplace_back(item.id);
				}
				if (selectedPasses.empty()) {
					diagnostic = "選択適用にPassがありません";
					return false;
				}
				for (UUID passID : selectedPasses) {
					const auto pass = std::find_if(profile.passes.begin(),
						profile.passes.end(), [passID](const auto& value) {

							return value.id == passID;
						});
					if (pass != profile.passes.end() &&
						pass->anchor != selection.anchor) {

						diagnostic =
							"選択適用とPassの実行位置が一致していません";
						return false;
					}
				}
				if (selection.mode ==
					RenderFeatureSelectionMode::IsolatedLayer) {

					isolatedSelections.emplace_back(&item);
				}
			}
			if (group && !validateItems(item.children,
				insideSelection || selective)) {

				return false;
			}
		}
		return true;
	};
	if (!validateItems(profile.hierarchy, false)) {
		return false;
	}
	for (size_t left = 0; left < isolatedSelections.size(); ++left) {
		for (size_t right = left + 1;
			right < isolatedSelections.size(); ++right) {

			const RenderFeatureSelectionSettings& a =
				isolatedSelections[left]->selection;
			const RenderFeatureSelectionSettings& b =
				isolatedSelections[right]->selection;
			if (a.anchor == b.anchor &&
				(a.renderingLayerMask & b.renderingLayerMask) != 0u &&
				(a.phaseMask & b.phaseMask) != 0u &&
				(a.rendererMask & b.rendererMask) != 0u) {

				diagnostic =
					"IsolatedLayerの抽出条件が重複しています";
				return false;
			}
		}
	}

	for (const RenderFeaturePassSettings& pass : profile.passes) {

		if (!pass.id || !passes.emplace(pass.id.value, &pass).second) {
			diagnostic = "RenderFeatureのPass IDが重複または未設定です";
			return false;
		}
		if (pass.name.empty() || !passNames.emplace(pass.name).second) {
			diagnostic = "RenderFeatureのPass名が重複または未設定です";
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

				diagnostic = "RenderFeatureの出力設定が不正です";
				return false;
			}
		}
		if (!pass.sceneColorOutput) {
			continue;
		}

		const RenderFeatureOutputSettings& output = defaultOutputs.front();
		if (output.format != RenderFeatureTextureFormat::Inherit ||
			output.widthScale != 1.0f || output.heightScale != 1.0f) {

			diagnostic =
				"SceneColor出力は継承形式かつ等倍が必要です";
			return false;
		}
		uint32_t& count = sceneColorCounts[
			GetRenderFeatureAnchorOrder(pass.anchor)];
		if (++count > 1u) {
			diagnostic = "同じ実行位置にSceneColor出力が複数あります";
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

			diagnostic = "RenderFeatureが存在しない出力を参照しています";
			return false;
		}
		if (GetRenderFeatureAnchorOrder(found->second->anchor) >
			GetRenderFeatureAnchorOrder(owner.anchor)) {

			diagnostic = "RenderFeatureが後段のPassを参照しています";
			return false;
		}
		return true;
	};

	for (const RenderFeaturePassSettings& pass : profile.passes) {

		if (!pass.enabled) {
			continue;
		}
		if (pass.sourceKind == RenderFeatureSourceKind::PassOutput &&
			!pass.source.pass) {

			diagnostic = "RenderFeatureの主入力Passが未設定です";
			return false;
		}
		if (pass.sourceKind == RenderFeatureSourceKind::PassOutput &&
			!validateReference(pass, pass.source)) {
			return false;
		}
		for (const auto& [resourceName, reference] : pass.passInputs) {

			if (resourceName.empty() || !validateReference(pass, reference)) {
				if (diagnostic.empty()) {
					diagnostic = "RenderFeatureの入力名が未設定です";
				}
				return false;
			}
		}
	}
	return true;
}
