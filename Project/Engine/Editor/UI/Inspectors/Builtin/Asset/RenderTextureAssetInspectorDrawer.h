#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/IAssetInspectorDrawer.h>
#include <Engine/Core/Rendering/Assets/RenderTextureAsset.h>
#include <string>

namespace Engine {

	//============================================================================
	//	RenderTextureAssetInspectorDrawer class
	//	Camera出力Textureのサイズを編集して保存する
	//============================================================================
	class RenderTextureAssetInspectorDrawer : public IAssetInspectorDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AssetType GetAssetType() const override { return AssetType::RenderTexture; }
		void Draw(const EditorPanelContext& context, const AssetMeta& meta) override;
		bool HasPendingChanges() const override { return draft_.width != saved_.width || draft_.height != saved_.height; }
		bool ApplyPendingChanges(const EditorPanelContext& context) override;
		void DiscardPendingChanges() override { draft_ = saved_; }
		AssetID GetEditingAsset() const override { return selectedAsset_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID selectedAsset_{};
		RenderTextureAsset saved_{};
		RenderTextureAsset draft_{};
		bool loaded_ = false;
		std::string status_;

		//--------- functions ----------------------------------------------------

		// 保存済みサイズを読み込む
		void Load(const EditorPanelContext& context, const AssetMeta& meta);
	};
}
