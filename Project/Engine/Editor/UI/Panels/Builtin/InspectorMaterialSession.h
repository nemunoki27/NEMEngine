#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

namespace Engine {

	struct AssetMeta;

	//============================================================================
	//	InspectorMaterialSession class
	//	Materialの編集値と保存を管理する
	//============================================================================
	class InspectorMaterialSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Materialアセットのインスペクターを描画する
		void DrawMaterialAssetInspector(const EditorPanelContext& context, const AssetMeta& meta);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Materialアセット編集用の一時データ
		AssetID editingMaterialAsset_{};
		MaterialAsset materialDraft_{};
		bool materialDraftValid_ = false;

		//--------- functions ----------------------------------------------------

		// Materialアセットの編集用データを読み込む
		bool LoadMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta);
		// Materialアセットの編集用データを保存する
		void SaveMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta);
	};
}
