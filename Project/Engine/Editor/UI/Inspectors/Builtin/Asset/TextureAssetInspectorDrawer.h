#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/IAssetInspectorDrawer.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	TextureAssetInspectorDrawer class
	//	Textureアセット選択時のプレビューと情報表示
	//============================================================================
	class TextureAssetInspectorDrawer :
		public IAssetInspectorDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AssetType GetAssetType() const override { return AssetType::Texture; }
		void Draw(const EditorPanelContext& context, const AssetMeta& meta) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID selectedAsset_{};
		TextureImportSettings savedSettings_{};
		TextureImportSettings draftSettings_{};
		TextureColorSpace previewColorSpace_ = TextureColorSpace::SRGB;
		TexturePreviewChannel previewChannel_ = TexturePreviewChannel::Color;
		std::string statusMessage_{};

		//--------- functions ----------------------------------------------------

		// 選択アセットが変わったとき編集状態を同期する
		void SyncSelection(const AssetMeta& meta);
		// Importer設定を描画する
		void DrawImportSettings(const EditorPanelContext& context, const AssetMeta& meta);
		// テクスチャプレビューと情報を描画する
		void DrawPreview(const EditorPanelContext& context, const AssetMeta& meta);
	};
} // Engine
