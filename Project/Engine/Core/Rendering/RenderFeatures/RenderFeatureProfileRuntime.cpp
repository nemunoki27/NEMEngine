#include "RenderFeatureProfileRuntime.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfileValidation.h"
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

	using namespace Engine::RenderFeatureProfileValidation;

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

	auto snapshot = std::make_shared<RenderFeatureProfileAsset>(profile);
	ApplyRenderFeatureHierarchy(*snapshot);
	profile_ = std::move(snapshot);
	selectionGroupsByPass_.clear();
	isolatedGroups_.clear();
	groupLineages_.clear();
	passLineages_.clear();
	CollectSelectionGroups(profile_->hierarchy, nullptr,
		selectionGroupsByPass_, isolatedGroups_, groupLineages_,
		passLineages_);
	diagnostic_.clear();
	RenderFeatureProfileValidation::Validate(*profile_, diagnostic_);
}

Engine::RenderFeatureExecutionPlan
Engine::RenderFeatureProfileRuntime::BuildPlan(
	RenderFeatureAnchor anchor, RenderViewKind viewKind) const {

	RenderFeatureExecutionPlan plan{};
	plan.profileSnapshot = profile_;
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

		const RenderFeaturePassRuntimeOverride* runtimeOverride =
			RenderFeatureRuntimeOverrides::GetInstance().Find(pass.id);
		const bool enabled = pass.enabled ||
			(runtimeOverride && runtimeOverride->enabled == true);
		return enabled && pass.material && pass.anchor == anchor &&
			IsEnabledForView(pass, viewKind) &&
			IsPassHierarchyEnabled(pass.id);
	};

	// 同じAnchor内の省略入力は直前Passの主出力へ接続する
	for (const RenderFeaturePassSettings& pass : profile_->passes) {

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
		if (RenderFeatureRuntimeOverrides::GetInstance().IsSceneColorOutput(
			pass.id, pass.sceneColorOutput)) {
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
				profile_->passes.begin(), profile_->passes.end(),
				[dependencyID](const RenderFeaturePassSettings& candidate) {

					return candidate.id.value == dependencyID;
				});
			if (external == profile_->passes.end() || !external->enabled ||
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
	for (const RenderFeaturePassSettings& pass : profile_->passes) {

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
	const auto pass = std::find_if(profile_->passes.begin(),
		profile_->passes.end(), [&item](const RenderFeaturePassSettings& value) {

			return value.id == item.id;
		});
	return pass != profile_->passes.end() && pass->enabled &&
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
