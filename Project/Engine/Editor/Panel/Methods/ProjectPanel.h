#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Editor/Project/ProjectAssetIndex.h>
#include <Engine/Editor/Project/ProjectAssetThumbnailCache.h>

namespace Engine {

	// front
	class TextureUploadService;

	//============================================================================
	//	ProjectPanel class
	//	プロジェクトパネル
	//============================================================================
	class ProjectPanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProjectPanel(TextureUploadService& textureUploadService);
		~ProjectPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// プロジェクト内のアセットのインデックスとサムネイルキャッシュ
		ProjectAssetIndex assetIndex_;
		ProjectAssetThumbnailCache thumbnailCache_;

		ProjectAssetSource assetSource_ = ProjectAssetSource::Engine;
		// 現在選択されているディレクトリの仮想パスとアセットID
		std::string selectedDirectory_ = "Engine/Assets";
		AssetID selectedAsset_{};

		bool dirty_ = true;

		//--------- functions ----------------------------------------------------

		// インデックスとサムネイルキャッシュを再構築する
		void Rebuild(AssetDatabase& database);
		// プロジェクトのディレクトリツリーと内容を描画する
		void DrawHeader(AssetDatabase& database);
		void DrawSourceSelector(AssetDatabase& database);
		void DrawDirectoryContents(const ProjectDirectoryNode& node);

		const char* GetSourceRootPath() const;
	};
} // Engine
