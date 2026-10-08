#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfile.h"

// c++
#include <unordered_set>

namespace Engine::RenderFeatureHierarchyEditing {

	// 階層内の編集位置
	struct HierarchyItemLocation {

		std::vector<RenderFeatureHierarchyItem>* siblings = nullptr; // 同じ親の項目列
		size_t index = 0;											 // 項目列内の位置
		RenderFeatureHierarchyItem* parentGroup = nullptr;			 // ルートでは親なし
	};

	// 対象の階層位置を取得
	bool FindItemLocation(std::vector<RenderFeatureHierarchyItem>& items, RenderFeatureHierarchyItemType type, UUID id,
		HierarchyItemLocation& outLocation, RenderFeatureHierarchyItem* parentGroup = nullptr);
	// 対象のPass設定を取得
	RenderFeaturePassSettings* FindFeaturePass(RenderFeatureProfileAsset& profile, UUID id);
	// 子孫のPassを収集
	void CollectPassIDs(const RenderFeatureHierarchyItem& item, std::unordered_set<uint64_t>& outPasses);
	// 階層と参照元のPassを削除
	bool DeleteItem(RenderFeatureProfileAsset& profile, RenderFeatureHierarchyItemType type, UUID id);
	// 選択Passが連続する兄弟か確認
	bool CanGroupSelection(RenderFeatureProfileAsset& profile, const std::vector<UUID>& selectedPasses);
	// 選択Passをグループ化
	UUID GroupSelection(RenderFeatureProfileAsset& profile, const std::vector<UUID>& selectedPasses);
	// Passを親グループの直後へ移動
	bool UngroupPass(RenderFeatureProfileAsset& profile, UUID passID);
	// 階層項目を指定グループへ移動
	bool MoveItemToGroup(
		RenderFeatureProfileAsset& profile, RenderFeatureHierarchyItemType type, UUID itemID, UUID targetGroupID);
}
