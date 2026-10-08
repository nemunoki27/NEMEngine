#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {

	// front
	struct EditorPanelContext;
	struct AssetMeta;

	//============================================================================
	//	IAssetInspectorDrawer class
	//	Asset選択時のInspector表示を種別ごとに実装する共通インターフェース
	//============================================================================
	class IAssetInspectorDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		virtual ~IAssetInspectorDrawer() = default;

		// 対象アセット種別
		virtual AssetType GetAssetType() const = 0;
		// 選択中アセットのInspector表示を行う
		virtual void Draw(const EditorPanelContext& context, const AssetMeta& meta) = 0;
		// 未確定の編集があるか
		virtual bool HasPendingChanges() const { return false; }
		// 未確定の編集を保存する
		virtual bool ApplyPendingChanges([[maybe_unused]] const EditorPanelContext& context) { return true; }
		// 未確定の編集を破棄する
		virtual void DiscardPendingChanges() {}
		// 編集対象のAssetを返す
		virtual AssetID GetEditingAsset() const { return {}; }
	};
} // Engine
