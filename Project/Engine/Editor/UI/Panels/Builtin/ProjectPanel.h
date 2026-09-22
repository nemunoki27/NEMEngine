#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include "ProjectModelPreview.h"
#include "ProjectSceneStorageInspector.h"
#include <Engine/Editor/Assets/Project/ProjectAssetIndex.h>
#include <Engine/Editor/Assets/Project/ProjectAssetThumbnailCache.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Editor/Assets/Project/AssetActionRegistry.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>

#include <memory>
#include <unordered_map>

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
		//============================================================================
		//	public Methods
		//============================================================================

		ProjectPanel(TextureUploadService& textureUploadService,
			const std::string& instanceID = "project.primary", bool primaryInstance = true,
			const std::string& displayName = "Project");
		~ProjectPanel();

		void Draw(const EditorPanelContext& context) override;
		nlohmann::json SaveLayoutState() const override;
		void LoadLayoutState(const nlohmann::json& state) override;
		nlohmann::json MakeDuplicateState(const EditorPanelContext& context) const override;

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
		bool CanDuplicate([[maybe_unused]] const EditorPanelContext& context) const override { return true; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// モデル一覧のプレビュー
		ProjectModelPreview modelPreview_;

		// プロジェクト内のアセットのインデックスとサムネイルキャッシュ
		ProjectAssetIndex assetIndex_;
		ProjectAssetThumbnailCache thumbnailCache_;
		// アセット種別ごとの操作登録
		AssetActionRegistry assetActionRegistry_{};
		// 表示対象にしているアセットソース
		ProjectAssetSource assetSource_ = ProjectAssetSource::Engine;
		// ウィンドウの表示名
		std::string displayName_ = "Project";
		// 現在選択されているディレクトリの仮想パスとアセットID
		std::string selectedDirectory_ = "Engine/Assets";
		AssetID selectedAsset_{};

		// 上部のファイル検索フィルタ、入力中は右側に一致ファイルの一覧を表示する
		TextSearchFilter fileSearchFilter_;

		// trueのときAssetDatabaseから表示用Indexを再構築する
		bool dirty_ = true;
		// 最後に取り込んだAssetDatabaseの構造リビジョン、外部のファイル追加削除を検知して再構築する
		uint64_t lastSeenStructureRevision_ = 0;

		// 新規作成ポップアップで作るアセット種別
		ProjectAssetFileKind pendingCreateKind_ = ProjectAssetFileKind::Folder;
		// 新規作成先ディレクトリの仮想パス
		std::string pendingCreateDirectory_;
		// 新規作成名の入力バッファ
		std::string createNameBuffer_;
		// 新規作成時に表示するエラーメッセージ
		std::string createErrorMessage_;
		// 新規作成ポップアップを次の描画で開くか
		bool requestOpenCreatePopup_ = false;
		// リネーム対象アセットの情報
		ProjectAssetEntry pendingRenameAsset_{};
		// リネーム対象がフォルダかどうか、trueならpendingRenameDirectoryPath_を使う
		bool pendingRenameIsDirectory_ = false;
		// リネーム対象フォルダの仮想パス
		std::string pendingRenameDirectoryPath_;
		// リネーム入力で編集できるファイル名部分
		std::string renameNameBuffer_;
		// リネーム時に固定表示する保護サフィックス
		std::string renameProtectedSuffix_;
		// リネーム時に表示するエラーメッセージ
		std::string renameErrorMessage_;
		// リネームポップアップを次の描画で開くか
		bool requestOpenRenamePopup_ = false;
		// 削除対象アセットの情報
		ProjectAssetEntry pendingDeleteAsset_{};
		// 削除対象を参照しているアセットのパス一覧(確認表示用)
		std::vector<std::string> pendingDeleteReferencers_;
		std::string deleteErrorMessage_;
		// シーン保存の検証と修復表示
		ProjectSceneStorageInspector sceneStorageInspector_;
		// 削除確認ポップアップを次の描画で開くか
		bool requestOpenDeletePopup_ = false;
		// ファイル操作結果の遅延反映用キャッシュ
		ProjectAssetFileResult pendingFileOperationResult_{};
		// ファイル操作後の再構築が保留されているか
		bool hasPendingFileOperationRefresh_ = false;
		// Ctrl+C/Ctrl+Vのコピペで控えるアセットの内部クリップボード
		ProjectAssetEntry copiedAsset_{};
		bool hasCopiedAsset_ = false;

		//--------- functions ----------------------------------------------------

		// 現在のAssetDatabaseから表示用インデックスを再構築する
		void RebuildIndex(const AssetDatabase& database);
		// AssetDatabaseを更新して表示用インデックスを再構築する
		void RefreshDatabaseAndIndex(AssetDatabase& database);
		// 外部エクスプローラーからドロップされたファイルをカレントフォルダへ取り込む
		void HandleExternalFileDrop(const EditorPanelContext& context, AssetDatabase& database);
		// 上部のファイル検索ボックスを描画する、左端に検索アイコンを重ねる
		void DrawSearchBar(const EditorPanelContext& context);
		// 右側コンテンツ上部のパンくずを描画する
		void DrawBreadcrumb(const EditorPanelContext& context, AssetDatabase& database);
		// Engine/Gameのソース切り替えを描画する
		void DrawSourceSelector(const EditorPanelContext& context, AssetDatabase& database);
		// 現在ディレクトリ内のフォルダとアセットを描画する
		void DrawDirectoryContents(const EditorPanelContext& context, AssetDatabase& database, const ProjectDirectoryNode& node);
		// 検索中に一致したフォルダとアセットを横断的に一覧表示する
		void DrawSearchResults(const EditorPanelContext& context, AssetDatabase& database);
		// 検索フィルタに一致するフォルダとアセットをツリー全体から集める
		void CollectSearchMatches(const ProjectDirectoryNode& node,
			std::vector<const ProjectDirectoryNode*>& outFolders,
			std::vector<const ProjectAssetEntry*>& outAssets) const;
		// グリッドのフォルダ1項目を描画する、クリックで移動し検索を解除する
		void DrawFolderGridItem(const EditorPanelContext& context, AssetDatabase& database,
			const ProjectDirectoryNode& node, float iconSize);
		// グリッドのアセット1項目を描画する
		void DrawAssetGridItem(const EditorPanelContext& context, AssetDatabase& database,
			const ProjectAssetEntry& asset, float iconSize);
		// アセット種別ごとのアイコンを解決する
		ImTextureID ResolveAssetIconTextureID(const ProjectAssetEntry& asset, ImVec2& outUV0, ImVec2& outUV1);
		// アセットのドラッグソースを描画する
		void DrawAssetDragDropSource(const ProjectAssetEntry& asset, ImGuiDragDropFlags flags = ImGuiDragDropFlags_None);
		// 空白部分の右クリックメニューを描画する
		void DrawDirectoryContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node);
		// フォルダ右クリックメニューを描画する
		void DrawFolderContextMenu(const EditorPanelContext& context, AssetDatabase& database, const ProjectDirectoryNode& node);
		// アセット右クリックメニューを描画する
		void DrawAssetContextMenu(const EditorPanelContext& context, AssetDatabase& database, const ProjectAssetEntry& asset);
		// 新規作成用の名前入力ポップアップを描画する
		void DrawCreateAssetPopup(AssetDatabase& database);
		// アセットリネーム用の名前入力ポップアップを描画する
		void DrawRenameAssetPopup(const EditorPanelContext& context, AssetDatabase& database);
		// 削除確認ポップアップを描画する(参照元があれば警告する)
		void DrawDeleteAssetPopup(const EditorPanelContext& context, AssetDatabase& database);
		// アセット種別ごとの操作をRegistryへ登録する
		void RegisterAssetActions();
		// アセットのダブルクリック操作を処理する
		void HandleAssetDoubleClick(const EditorPanelContext& context, const ProjectAssetEntry& asset);
		// HierarchyからドロップされたEntityをPrefabとして保存する
		bool SaveDroppedEntityAsPrefab(const EditorPanelContext& context, AssetDatabase& database,
			const std::string& directoryVirtualPath, const void* payloadData, int32_t payloadSize);
		// Hierarchy EntityのPrefab化ドロップ先を描画する
		void DrawPrefabCreateDropTarget(const EditorPanelContext& context, AssetDatabase& database,
			const std::string& directoryVirtualPath);
		// Project内ファイル/フォルダ移動のドロップ先を描画する
		void DrawProjectItemMoveDropTarget(AssetDatabase& database, const std::string& targetDirectoryVirtualPath);
		// Project内ファイル/フォルダ移動を実行する
		bool MoveDroppedProjectItem(AssetDatabase& database, const std::string& targetDirectoryVirtualPath,
			const void* payloadData, int32_t payloadSize);
		// 作成処理の入力状態を初期化する
		void BeginCreateAsset(ProjectAssetFileKind kind, const std::string& directoryVirtualPath);
		// リネーム処理の入力状態を初期化する
		void BeginRenameAsset(const ProjectAssetEntry& asset);
		// フォルダのリネーム入力状態を初期化する
		void BeginRenameDirectory(const ProjectDirectoryNode& node);
		// 作成メニュー項目を描画する
		void DrawCreateMenuItems(const std::string& directoryVirtualPath);
		// ファイル操作後にAssetDatabaseと表示を更新する
		void RefreshAfterFileOperation(AssetDatabase& database, const ProjectAssetFileResult& result);
		// 遅延していたファイル操作後の更新を適用する
		void ApplyPendingFileOperationRefresh(AssetDatabase& database);
		// 前回閉じた時の表示ディレクトリを読み込む
		void LoadPersistentState();
		// 現在表示しているディレクトリを保存する
		void SavePersistentState() const;

		// 現在のソースルート表示名を取得する
		const char* GetSourceRootPath() const;
	};
} // Engine
