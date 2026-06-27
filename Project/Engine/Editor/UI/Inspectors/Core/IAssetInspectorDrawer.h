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
	};
} // Engine
