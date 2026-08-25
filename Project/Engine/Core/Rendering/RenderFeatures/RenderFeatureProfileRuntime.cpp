#include "RenderFeatureProfileRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

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

	void CollectSelectionGroups(
		const std::vector<Engine::RenderFeatureHierarchyItem>& items,
		const Engine::RenderFeatureHierarchyItem* parentSelection,
		std::unordered_map<uint64_t,
			const Engine::RenderFeatureHierarchyItem*>& outGroups,
		std::vector<const Engine::RenderFeatureHierarchyItem*>& outIsolated,
		std::unordered_map<const Engine::RenderFeatureHierarchyItem*,
			std::vector<const Engine::RenderFeatureHierarchyItem*>>& outLineages,
		std::unordered_map<uint64_t,
			std::vector<const Engine::RenderFeatureHierarchyItem*>>& outPassLineages,
		std::vector<const Engine::RenderFeatureHierarchyItem*> lineage = {}) {

		for (const Engine::RenderFeatureHierarchyItem& item : items) {
			if (item.type ==
				Engine::RenderFeatureHierarchyItemType::Pass) {

				const Engine::RenderFeatureHierarchyItem* selection =
					item.selection.mode ==
						Engine::RenderFeatureSelectionMode::Organization ?
							parentSelection : &item;
				if (selection) {
					outGroups[item.id.value] = selection;
				}
				if (item.selection.mode ==
					Engine::RenderFeatureSelectionMode::IsolatedLayer) {

					outIsolated.emplace_back(&item);
				}
				outPassLineages[item.id.value] = lineage;
				continue;
			}
			const Engine::RenderFeatureHierarchyItem* selection =
				item.selection.mode ==
					Engine::RenderFeatureSelectionMode::Organization ?
						parentSelection : &item;
			lineage.emplace_back(&item);
			outLineages[&item] = lineage;
			if (item.selection.mode ==
				Engine::RenderFeatureSelectionMode::IsolatedLayer) {

				outIsolated.emplace_back(&item);
			}
			CollectSelectionGroups(item.children, selection, outGroups,
				outIsolated, outLineages, outPassLineages, lineage);
			lineage.pop_back();
		}
	}

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

	uint32_t GetRendererMaskBit(uint32_t backendID) {

		switch (backendID) {
		case Engine::RenderBackendID::Mesh:
			return Engine::RenderFeatureRendererMask::Mesh;
		case Engine::RenderBackendID::Primitive:
			return Engine::RenderFeatureRendererMask::Primitive;
		case Engine::RenderBackendID::Sprite:
			return Engine::RenderFeatureRendererMask::Sprite;
		case Engine::RenderBackendID::Text:
			return Engine::RenderFeatureRendererMask::Text;
		case Engine::RenderBackendID::Line:
			return Engine::RenderFeatureRendererMask::Line;
		case Engine::RenderBackendID::Particle:
			return Engine::RenderFeatureRendererMask::Particle;
		default:
			return 0u;
		}
	}

	bool IsEnabledForView(
		const Engine::RenderFeaturePassSettings& pass,
		Engine::RenderViewKind viewKind) {

		return (viewKind == Engine::RenderViewKind::Game && pass.gameView) ||
			(viewKind == Engine::RenderViewKind::Scene && pass.sceneView);
	}
}

//============================================================================
//	RenderFeatureProfileRuntime classMethods
//============================================================================
void Engine::RenderFeatureProfileRuntime::Rebuild(
	const RenderFeatureProfileAsset& profile) {

	profile_ = profile;
	ApplyRenderFeatureHierarchy(profile_);
	selectionGroupsByPass_.clear();
	isolatedGroups_.clear();
	groupLineages_.clear();
	passLineages_.clear();
	CollectSelectionGroups(profile_.hierarchy, nullptr,
		selectionGroupsByPass_, isolatedGroups_, groupLineages_,
		passLineages_);
	diagnostic_.clear();
	ValidateProfile();
}

Engine::RenderFeatureExecutionPlan
Engine::RenderFeatureProfileRuntime::BuildPlan(
	RenderFeatureAnchor anchor, RenderViewKind viewKind) const {

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
	const auto isActive = [&](const RenderFeaturePassSettings& pass) {

		return pass.enabled && pass.material && pass.anchor == anchor &&
			IsEnabledForView(pass, viewKind) &&
			IsPassHierarchyEnabled(pass.id);
	};

	// 同じAnchor内の省略入力は直前Passの主出力へ接続する
	for (const RenderFeaturePassSettings& pass : profile_.passes) {

		if (!isActive(pass)) {
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
				!external->material ||
				!IsEnabledForView(*external, viewKind) ||
				!IsPassHierarchyEnabled(external->id) ||
				GetRenderFeatureAnchorOrder(anchor) <=
				GetRenderFeatureAnchorOrder(external->anchor)) {

				plan.diagnostic =
					"無効または後段のRenderFeatureを参照しています";
				return false;
			}
		}

		state = 2u;
		const auto selection = selectionGroupsByPass_.find(pass.id.value);
		plan.nodes.emplace_back(RenderFeaturePlanNode{
			.pass = &pass,
			.selectionGroup = selection == selectionGroupsByPass_.end() ?
				nullptr : selection->second,
			.source = source,
		});
		return true;
	};

	// 出力未接続のPassも副作用を持てるためすべて実行計画へ含める
	for (const RenderFeaturePassSettings& pass : profile_.passes) {

		if (!isActive(pass)) {
			continue;
		}
		if (!visit(pass)) {
			plan.nodes.clear();
			return plan;
		}
	}
	for (size_t index = 0; index < plan.nodes.size(); ++index) {

		RenderFeaturePlanNode& node = plan.nodes[index];
		if (!node.selectionGroup) {
			continue;
		}
		node.selectionBegin = index == 0 ||
			plan.nodes[index - 1].selectionGroup != node.selectionGroup;
		node.selectionEnd = index + 1 == plan.nodes.size() ||
			plan.nodes[index + 1].selectionGroup != node.selectionGroup;
	}
	return plan;
}

bool Engine::RenderFeatureProfileRuntime::IsItemIsolated(
	const RenderItem& item) const {

	if (!diagnostic_.empty()) {
		return false;
	}
	for (const RenderFeatureHierarchyItem* selection : isolatedGroups_) {
		if (selection && MatchesRenderFeatureSelection(
			item, selection->selection) && IsSelectionEnabled(*selection)) {

			return true;
		}
	}
	return false;
}

bool Engine::RenderFeatureProfileRuntime::IsSelectionEnabled(
	const RenderFeatureHierarchyItem& item) const {

	if (item.type == RenderFeatureHierarchyItemType::Group) {
		return IsGroupEnabled(item);
	}
	const auto pass = std::find_if(profile_.passes.begin(),
		profile_.passes.end(), [&item](const RenderFeaturePassSettings& value) {

			return value.id == item.id;
		});
	return pass != profile_.passes.end() && pass->enabled &&
		IsPassHierarchyEnabled(pass->id);
}

bool Engine::RenderFeatureProfileRuntime::IsGroupEnabled(
	const RenderFeatureHierarchyItem& group) const {

	const auto lineage = groupLineages_.find(&group);
	if (lineage == groupLineages_.end()) {
		return false;
	}
	for (const RenderFeatureHierarchyItem* entry : lineage->second) {
		if (!entry || !entry->enabled ||
			!RenderFeatureRuntimeOverrides::GetInstance().IsGroupEnabled(
				entry->name, true)) {

			return false;
		}
	}
	return true;
}

bool Engine::RenderFeatureProfileRuntime::IsPassHierarchyEnabled(
	UUID passID) const {

	const auto lineage = passLineages_.find(passID.value);
	if (lineage == passLineages_.end()) {
		return true;
	}
	for (const RenderFeatureHierarchyItem* group : lineage->second) {
		if (!group || !IsGroupEnabled(*group)) {
			return false;
		}
	}
	return true;
}

bool Engine::MatchesRenderFeatureSelection(const RenderItem& item,
	const RenderFeatureSelectionSettings& selection) {

	return (item.renderingLayerMask &
		selection.renderingLayerMask) != 0u &&
		(selection.phaseMask & MakeRenderFeaturePhaseMask(
			item.renderPhase)) != 0u &&
		(selection.rendererMask &
			GetRendererMaskBit(item.backendID)) != 0u;
}

bool Engine::RenderFeatureProfileRuntime::ValidateProfile() {

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

				diagnostic_ =
					"RenderFeatureのグループ名が重複または未設定です";
				return false;
			}
			const bool selective = item.selection.mode !=
				RenderFeatureSelectionMode::Organization;
			if (selective && insideSelection) {
				diagnostic_ =
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

					diagnostic_ =
						"選択適用の抽出条件が空です";
					return false;
				}
				if (selection.mode ==
					RenderFeatureSelectionMode::IsolatedLayer &&
					(selection.phaseMask & MakeRenderFeaturePhaseMask(
						RenderPhase::Opaque)) != 0u) {

					diagnostic_ =
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
					diagnostic_ = "選択適用にPassがありません";
					return false;
				}
				for (UUID passID : selectedPasses) {
					const auto pass = std::find_if(profile_.passes.begin(),
						profile_.passes.end(), [passID](const auto& value) {

							return value.id == passID;
						});
					if (pass != profile_.passes.end() &&
						pass->anchor != selection.anchor) {

						diagnostic_ =
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
	if (!validateItems(profile_.hierarchy, false)) {
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

				diagnostic_ =
					"IsolatedLayerの抽出条件が重複しています";
				return false;
			}
		}
	}

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
			continue;
		}

		const RenderFeatureOutputSettings& output = defaultOutputs.front();
		if (output.format != RenderFeatureTextureFormat::Inherit ||
			output.widthScale != 1.0f || output.heightScale != 1.0f) {

			diagnostic_ =
				"SceneColor出力は継承形式かつ等倍が必要です";
			return false;
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
