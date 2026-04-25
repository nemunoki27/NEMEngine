#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Editor/Project/ProjectAssetIndex.h>
#include <Engine/Editor/Project/ProjectAssetThumbnailCache.h>
#include <Engine/Editor/Project/ProjectAssetFileUtility.h>

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

		ProjectAssetFileKind pendingCreateKind_ = ProjectAssetFileKind::Folder;
		std::string pendingCreateDirectory_;
		std::string createNameBuffer_;
		std::string createErrorMessage_;
		bool requestOpenCreatePopup_ = false;

		//--------- functions ----------------------------------------------------

		// インデックスとサムネイルキャッシュを再構築する
		void Rebuild(AssetDatabase& database);
		// ヘッダー部分のパンくずと更新ボタンを描画する
		void DrawHeader(AssetDatabase& database);
		// Engine/Gameのソース切り替えを描画する
		void DrawSourceSelector(AssetDatabase& database);
		// 現在ディレクトリ内のフォルダとアセットを描画する
		void DrawDirectoryContents(const EditorPanelContext& context, AssetDatabase& database, const ProjectDirectoryNode& node);
		// 空白部分の右クリックメニューを描画する
		void DrawDirectoryContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node);
		// フォルダ右クリックメニューを描画する
		void DrawFolderContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node);
		// アセット右クリックメニューを描画する
		void DrawAssetContextMenu(const EditorPanelContext& context, AssetDatabase& database, const ProjectAssetEntry& asset);
		// 新規作成用の名前入力ポップアップを描画する
		void DrawCreateAssetPopup(AssetDatabase& database);
		// アセットのダブルクリック操作を処理する
		void HandleAssetDoubleClick(const EditorPanelContext& context, const ProjectAssetEntry& asset);
		// 作成処理の入力状態を初期化する
		void BeginCreateAsset(ProjectAssetFileKind kind, const std::string& directoryVirtualPath);
		// 作成メニュー項目を描画する
		void DrawCreateMenuItems(const std::string& directoryVirtualPath);
		// ファイル操作後にAssetDatabaseと表示を更新する
		void RefreshAfterFileOperation(AssetDatabase& database, const ProjectAssetFileResult& result);

		// 現在のソースルート表示名を取得する
		const char* GetSourceRootPath() const;
	};
} // Engine
