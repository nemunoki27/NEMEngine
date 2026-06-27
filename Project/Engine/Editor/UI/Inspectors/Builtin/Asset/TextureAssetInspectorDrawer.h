#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/IAssetInspectorDrawer.h>

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
	};
} // Engine
