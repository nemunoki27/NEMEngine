#include "RenderFeatureProfile.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace {

	void NormalizeItems(
		std::vector<Engine::RenderFeatureHierarchyItem>& items,
		const std::unordered_set<uint64_t>& availablePasses,
		std::unordered_set<uint64_t>& registeredPasses,
		std::unordered_set<uint64_t>& registeredGroups) {

		std::erase_if(items, [&](Engine::RenderFeatureHierarchyItem& item) {

			if (!item.id) {
				return true;
			}
			if (item.type == Engine::RenderFeatureHierarchyItemType::Pass) {
				item.children.clear();
				return !availablePasses.contains(item.id.value) ||
					!registeredPasses.emplace(item.id.value).second;
			}
			if (!registeredGroups.emplace(item.id.value).second) {
				return true;
			}
			if (item.name.empty()) {
				item.name = "グループ";
			}
			NormalizeItems(item.children, availablePasses,
				registeredPasses, registeredGroups);
			return false;
		});
	}

	void CollectPassOrder(
		const std::vector<Engine::RenderFeatureHierarchyItem>& items,
		bool parentEnabled, std::vector<Engine::UUID>& order,
		std::unordered_map<uint64_t, bool>& effectiveEnabled) {

		for (const Engine::RenderFeatureHierarchyItem& item : items) {
			if (item.type == Engine::RenderFeatureHierarchyItemType::Pass) {
				order.emplace_back(item.id);
				effectiveEnabled[item.id.value] = parentEnabled;
				continue;
			}
			CollectPassOrder(item.children, parentEnabled && item.enabled,
				order, effectiveEnabled);
		}
	}

	void ReorderPasses(Engine::RenderFeatureProfileAsset& profile,
		const std::vector<Engine::UUID>& order) {

		std::unordered_map<uint64_t, size_t> orderByID{};
		for (size_t index = 0; index < order.size(); ++index) {
			orderByID.emplace(order[index].value, index);
		}
		std::stable_sort(profile.passes.begin(), profile.passes.end(),
			[&](const Engine::RenderFeaturePassSettings& left,
				const Engine::RenderFeaturePassSettings& right) {

				const auto leftOrder = orderByID.find(left.id.value);
				const auto rightOrder = orderByID.find(right.id.value);
				if (leftOrder == orderByID.end()) {
					return false;
				}
				if (rightOrder == orderByID.end()) {
					return true;
				}
				return leftOrder->second < rightOrder->second;
			});
	}

	void SortSelections(
		std::vector<Engine::RenderFeatureHierarchyItem>& items) {

		for (Engine::RenderFeatureHierarchyItem& item : items) {
			if (item.type == Engine::RenderFeatureHierarchyItemType::Group) {
				SortSelections(item.children);
			}
		}
		for (uint32_t anchorIndex = 0u;
			anchorIndex <= static_cast<uint32_t>(
				Engine::RenderFeatureAnchor::AfterToneMap); ++anchorIndex) {

			const Engine::RenderFeatureAnchor anchor =
				static_cast<Engine::RenderFeatureAnchor>(anchorIndex);
			std::vector<size_t> slots{};
			std::vector<Engine::RenderFeatureHierarchyItem> selections{};
			for (size_t index = 0u; index < items.size(); ++index) {
				const Engine::RenderFeatureHierarchyItem& item = items[index];
				if (item.selection.mode ==
						Engine::RenderFeatureSelectionMode::IsolatedLayer &&
					item.selection.anchor == anchor) {

					slots.emplace_back(index);
					selections.emplace_back(item);
				}
			}
			std::stable_sort(selections.begin(), selections.end(),
				[](const Engine::RenderFeatureHierarchyItem& left,
					const Engine::RenderFeatureHierarchyItem& right) {

					if (left.selection.sortingLayer !=
						right.selection.sortingLayer) {

						return left.selection.sortingLayer <
							right.selection.sortingLayer;
					}
					return left.selection.sortingOrder <
						right.selection.sortingOrder;
				});
			for (size_t index = 0u; index < slots.size(); ++index) {
				items[slots[index]] = std::move(selections[index]);
			}
		}
	}
}

//============================================================================
//	RenderFeatureProfile functions
//============================================================================
uint32_t Engine::GetRenderFeatureAnchorOrder(
	RenderFeatureAnchor anchor) {

	return static_cast<uint32_t>(anchor);
}

void Engine::CopyRenderFeatureProfileSettings(
	RenderFeatureProfileAsset& destination,
	const RenderFeatureProfileAsset& source) {

	const AssetID guid = destination.guid;
	const std::string name = destination.name;
	const uint32_t version = destination.version;
	destination = source;
	destination.guid = guid;
	destination.name = name;
	destination.version = version;
	SynchronizeRenderFeaturePassOrder(destination);
}

void Engine::NormalizeRenderFeatureHierarchy(
	RenderFeatureProfileAsset& profile) {

	std::unordered_set<uint64_t> availablePasses{};
	for (const RenderFeaturePassSettings& pass : profile.passes) {
		if (pass.id) {
			availablePasses.emplace(pass.id.value);
		}
	}

	std::unordered_set<uint64_t> registeredPasses{};
	std::unordered_set<uint64_t> registeredGroups{};
	NormalizeItems(profile.hierarchy, availablePasses,
		registeredPasses, registeredGroups);

	for (const RenderFeaturePassSettings& pass : profile.passes) {
		if (!pass.id || registeredPasses.contains(pass.id.value)) {
			continue;
		}
		profile.hierarchy.emplace_back(RenderFeatureHierarchyItem{
			.type = RenderFeatureHierarchyItemType::Pass,
			.id = pass.id,
		});
	}
}

void Engine::ApplyRenderFeatureHierarchy(
	RenderFeatureProfileAsset& profile) {

	NormalizeRenderFeatureHierarchy(profile);
	SortSelections(profile.hierarchy);
	std::vector<UUID> order{};
	std::unordered_map<uint64_t, bool> effectiveEnabled{};
	CollectPassOrder(profile.hierarchy, true, order, effectiveEnabled);
	ReorderPasses(profile, order);

	for (RenderFeaturePassSettings& pass : profile.passes) {
		const auto found = effectiveEnabled.find(pass.id.value);
		pass.enabled = pass.enabled && found != effectiveEnabled.end() &&
			found->second;
	}
}

void Engine::SynchronizeRenderFeaturePassOrder(
	RenderFeatureProfileAsset& profile) {

	NormalizeRenderFeatureHierarchy(profile);
	SortSelections(profile.hierarchy);
	std::vector<UUID> order{};
	std::unordered_map<uint64_t, bool> effectiveEnabled{};
	CollectPassOrder(profile.hierarchy, true, order, effectiveEnabled);
	ReorderPasses(profile, order);
}
