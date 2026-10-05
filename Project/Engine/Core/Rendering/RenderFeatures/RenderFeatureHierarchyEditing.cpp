#include "RenderFeatureHierarchyEditing.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <string_view>

namespace Engine::RenderFeatureHierarchyEditing {

	using HierarchyItem = Engine::RenderFeatureHierarchyItem;
	using HierarchyItemType = Engine::RenderFeatureHierarchyItemType;

	bool FindItemLocation(std::vector<HierarchyItem>& items, HierarchyItemType type, Engine::UUID id,
		HierarchyItemLocation& outLocation, HierarchyItem* parentGroup) {

		// 子孫を含めて編集対象の位置を検索
		for (size_t index = 0; index < items.size(); ++index) {
			HierarchyItem& item = items[index];
			if (item.type == type && item.id == id) {
				outLocation = {
					.siblings = &items,
					.index = index,
					.parentGroup = parentGroup,
				};
				return true;
			}
			if (item.type == HierarchyItemType::Group && FindItemLocation(item.children, type, id, outLocation, &item)) {

				return true;
			}
		}
		return false;
	}

	const HierarchyItem* FindItem(const std::vector<HierarchyItem>& items, HierarchyItemType type, Engine::UUID id) {

		for (const HierarchyItem& item : items) {
			if (item.type == type && item.id == id) {
				return &item;
			}
			if (item.type == HierarchyItemType::Group) {
				if (const HierarchyItem* found = FindItem(item.children, type, id)) {

					return found;
				}
			}
		}
		return nullptr;
	}

	Engine::RenderFeaturePassSettings* FindFeaturePass(Engine::RenderFeatureProfileAsset& profile, Engine::UUID id) {

		const auto found =
			std::find_if(profile.passes.begin(), profile.passes.end(), [id](const auto& pass) { return pass.id == id; });
		return found == profile.passes.end() ? nullptr : &*found;
	}

	void CollectPassIDs(const HierarchyItem& item, std::unordered_set<uint64_t>& outPasses) {

		if (item.type == HierarchyItemType::Pass) {
			outPasses.emplace(item.id.value);
			return;
		}
		for (const HierarchyItem& child : item.children) {
			CollectPassIDs(child, outPasses);
		}
	}

	void ErasePasses(Engine::RenderFeatureProfileAsset& profile, const std::unordered_set<uint64_t>& passIDs) {

		// 削除Passを参照する入出力も解除
		std::erase_if(profile.passes, [&](const auto& pass) { return passIDs.contains(pass.id.value); });
		for (Engine::RenderFeaturePassSettings& pass : profile.passes) {
			if (pass.source.pass && passIDs.contains(pass.source.pass.value)) {
				pass.sourceKind = Engine::RenderFeatureSourceKind::PreviousPass;
				pass.source = {};
			}
			std::erase_if(pass.passInputs,
				[&](const auto& input) { return input.second.pass && passIDs.contains(input.second.pass.value); });
		}
	}

	bool DeleteItem(Engine::RenderFeatureProfileAsset& profile, HierarchyItemType type, Engine::UUID id) {

		HierarchyItemLocation location{};
		if (!FindItemLocation(profile.hierarchy, type, id, location)) {
			return false;
		}
		std::unordered_set<uint64_t> passIDs{};
		// 削除範囲を確定して階層とPassを同期
		CollectPassIDs((*location.siblings)[location.index], passIDs);
		location.siblings->erase(location.siblings->begin() + location.index);
		ErasePasses(profile, passIDs);
		Engine::SynchronizeRenderFeaturePassOrder(profile);
		return true;
	}

	bool CanGroupSelection(Engine::RenderFeatureProfileAsset& profile, const std::vector<Engine::UUID>& selectedPasses) {

		if (selectedPasses.size() < 2) {
			return false;
		}
		std::vector<size_t> indices{};
		std::vector<HierarchyItem>* siblings = nullptr;
		for (Engine::UUID passID : selectedPasses) {
			HierarchyItemLocation location{};
			if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Pass, passID, location)) {

				return false;
			}
			if (siblings && siblings != location.siblings) {
				return false;
			}
			siblings = location.siblings;
			indices.emplace_back(location.index);
		}
		std::ranges::sort(indices);
		// 連続した兄弟以外はグループ化しない
		return std::adjacent_find(indices.begin(), indices.end(),
				   [](size_t left, size_t right) { return right != left + 1; }) == indices.end();
	}

	bool HasGroupName(const std::vector<HierarchyItem>& items, std::string_view name) {

		for (const HierarchyItem& item : items) {
			if (item.type != HierarchyItemType::Group) {
				continue;
			}
			if (item.name == name || HasGroupName(item.children, name)) {
				return true;
			}
		}
		return false;
	}

	std::string MakeGroupName(const Engine::RenderFeatureProfileAsset& profile) {

		std::string name = "グループ";
		// 既存グループと重ならない名前を作成
		for (uint32_t suffix = 2; HasGroupName(profile.hierarchy, name); ++suffix) {

			name = "グループ " + std::to_string(suffix);
		}
		return name;
	}

	Engine::UUID GroupSelection(Engine::RenderFeatureProfileAsset& profile, const std::vector<Engine::UUID>& selectedPasses) {

		if (!CanGroupSelection(profile, selectedPasses)) {
			return {};
		}
		std::vector<size_t> indices{};
		std::vector<HierarchyItem>* siblings = nullptr;
		for (Engine::UUID passID : selectedPasses) {
			HierarchyItemLocation location{};
			FindItemLocation(profile.hierarchy, HierarchyItemType::Pass, passID, location);
			siblings = location.siblings;
			indices.emplace_back(location.index);
		}
		std::ranges::sort(indices);
		const size_t firstIndex = indices.front();
		const size_t lastIndex = indices.back();

		HierarchyItem group{
			.type = HierarchyItemType::Group,
			.id = Engine::UUID::New(),
			.name = MakeGroupName(profile),
		};
		// 子要素の領域を確保してから選択Passを移動
		group.children.reserve(lastIndex - firstIndex + 1);
		for (size_t index = firstIndex; index <= lastIndex; ++index) {
			group.children.emplace_back(std::move((*siblings)[index]));
		}
		siblings->erase(siblings->begin() + firstIndex, siblings->begin() + lastIndex + 1);
		const Engine::UUID groupID = group.id;
		siblings->insert(siblings->begin() + firstIndex, std::move(group));
		Engine::SynchronizeRenderFeaturePassOrder(profile);
		return groupID;
	}

	bool UngroupPass(Engine::RenderFeatureProfileAsset& profile, Engine::UUID passID) {

		HierarchyItemLocation passLocation{};
		if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Pass, passID, passLocation) || !passLocation.parentGroup) {

			return false;
		}
		const Engine::UUID parentGroupID = passLocation.parentGroup->id;
		HierarchyItemLocation groupLocation{};
		if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Group, parentGroupID, groupLocation)) {

			return false;
		}
		// 挿入先の確保で変わった階層位置を取り直す
		groupLocation.siblings->reserve(groupLocation.siblings->size() + 1);
		FindItemLocation(profile.hierarchy, HierarchyItemType::Pass, passID, passLocation);
		HierarchyItem pass = std::move((*passLocation.siblings)[passLocation.index]);
		passLocation.siblings->erase(passLocation.siblings->begin() + passLocation.index);

		if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Group, parentGroupID, groupLocation)) {

			return false;
		}
		groupLocation.siblings->insert(groupLocation.siblings->begin() + groupLocation.index + 1, std::move(pass));
		Engine::SynchronizeRenderFeaturePassOrder(profile);
		return true;
	}

	bool MoveItemToGroup(
		Engine::RenderFeatureProfileAsset& profile, HierarchyItemType type, Engine::UUID itemID, Engine::UUID targetGroupID) {

		if (type == HierarchyItemType::Group && itemID == targetGroupID) {
			return false;
		}
		const HierarchyItem* source = FindItem(profile.hierarchy, type, itemID);
		if (!source) {
			return false;
		}
		if (type == HierarchyItemType::Group && FindItem(source->children, HierarchyItemType::Group, targetGroupID)) {

			return false;
		}

		// 移動先を確認してから元の項目を取り外す
		HierarchyItemLocation targetLocation{};
		if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Group, targetGroupID, targetLocation)) {

			return false;
		}
		std::vector<HierarchyItem>& children = (*targetLocation.siblings)[targetLocation.index].children;
		children.reserve(children.size() + 1);
		HierarchyItemLocation sourceLocation{};
		if (!FindItemLocation(profile.hierarchy, type, itemID, sourceLocation)) {

			return false;
		}
		HierarchyItem moved = std::move((*sourceLocation.siblings)[sourceLocation.index]);
		sourceLocation.siblings->erase(sourceLocation.siblings->begin() + sourceLocation.index);

		if (!FindItemLocation(profile.hierarchy, HierarchyItemType::Group, targetGroupID, targetLocation)) {

			return false;
		}
		(*targetLocation.siblings)[targetLocation.index].children.emplace_back(std::move(moved));
		Engine::SynchronizeRenderFeaturePassOrder(profile);
		return true;
	}

}
