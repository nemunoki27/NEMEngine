#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
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
		public IEditorPanel,
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ProjectPanel(TextureUploadService& textureUploadService,
			const std::string& instanceID = "project.primary", bool primaryInstance = true,
			const std::string& displayName = "Project");
		~ProjectPanel();

		void Draw(const EditorPanelContext& context) override;
		void DrawEditorTool(const EditorToolContext& context) override;
		nlohmann::json SaveLayoutState() const override;
		void LoadLayoutState(const nlohmann::json& state) override;
		nlohmann::json MakeDuplicateState(const EditorPanelContext& context) const override;

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
		bool CanDuplicate([[maybe_unused]] const EditorPanelContext& context) const override { return true; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// プロジェクト内のアセットのインデックスとサムネイルキャッシュ
		ProjectAssetIndex assetIndex_;
		ProjectAssetThumbnailCache thumbnailCache_;
		// アセット種別ごとの操作登録
		AssetActionRegistry assetActionRegistry_{};
		// モデルプレビューAtlas用の内部EditorTool定義
		ToolDescriptor descriptor_{
			.id = "engine.project_panel",
			.name = "ProjectPanel",
			.category = "Editor",
			.description = "Internal ProjectPanel preview renderer.",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = -100,
		};

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

		struct ModelPreviewBounds {

			// モデル頂点全体の最小座標
			Vector3 min = Vector3::AnyInit(0.0f);
			// モデル頂点全体の最大座標
			Vector3 max = Vector3::AnyInit(0.0f);
			// プレビューカメラの注視点に使う境界中心
			Vector3 center = Vector3::AnyInit(0.0f);
			// モデル全体を収めるための境界半径
			float radius = 1.0f;
			// 境界計算が成功しているか
			bool valid = false;
		};

		struct ModelPreviewSlot {

			// このスロットに対応するモデルアセットID
			AssetID assetID{};
			// 再構築判定とデバッグ用に保持するモデル仮想パス
			std::string assetPath;
			// プレビュー専用World内で描画するEntity
			Entity entity = Entity::Null();
			// Atlas内の左上ピクセル座標
			Vector2I pixelPos;
			// Atlas内の割り当てサイズ
			Vector2I pixelSize;
			// ImGui::ImageでAtlasから切り出す左上UV
			ImVec2 uv0{};
			// ImGui::ImageでAtlasから切り出す右下UV
			ImVec2 uv1{};
			// スロットごとのカメラ計算に使うモデル境界
			ModelPreviewBounds bounds{};
		};

		struct ModelPreviewAppearanceSettings {

			// 1モデルに割り当てるAtlas上の正方形スロットサイズ
			int32_t tileSize = 192;
			// プレビューAtlasをクリアするときの背景色、暗めの青
			Color4 clearColor = Color4(0.04f, 0.06f, 0.16f, 1.0f);
			// モデルを収めるためのカメラ縦視野角
			float cameraFovY = 35.0f;
			// 境界半径から求めたカメラ距離に掛ける倍率、近づける
			float cameraDistanceScale = 0.8f;
			// 上から見下ろすためのカメラPitch角
			float cameraPitchDegrees = 25.0f;
			// 右寄りから見る3/4ビューにするためのカメラYaw角
			float cameraYawDegrees = 215.0f;
			// プレビューWorldで使うDirectionalLightの向き
			Vector3 lightDirection = Vector3(0.35f, -0.65f, 0.65f);
			// プレビューWorldで使うDirectionalLightの強さ
			float lightIntensity = 1.5f;
		};

		// モデルプレビューの見た目に関する調整パラメータ、初期値固定で表示する
		ModelPreviewAppearanceSettings modelPreviewSettings_{};
		// モデルプレビュー専用の一時World
		std::unique_ptr<ECSWorld> modelPreviewWorld_;
		// プレビューWorld内のライトEntity
		Entity modelPreviewLightEntity_ = Entity::Null();
		// Atlas内のモデルごとの割り当て情報
		std::vector<ModelPreviewSlot> modelPreviewSlots_;
		// AssetIDからAtlasスロットを引くためのIndex
		std::unordered_map<AssetID, size_t> modelPreviewSlotByAsset_;
		// 現在Atlasを構築しているディレクトリ
		std::string modelPreviewDirectory_;
		// 現在Atlasを構築しているアセット一覧の署名
		uint64_t modelPreviewSignature_ = 0;
		// 1枚だけ持つモデルプレビューAtlasのピクセルサイズ
		Vector2I modelPreviewAtlasSize_;
		// モデル読込完了までAtlasを更新する残りフレーム数
		uint32_t modelPreviewRefreshFrames_ = 0;

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
		// ファイル変更時と明示操作時だけシーンの整合性を検査する
		uint64_t sceneStorageRevision_ = 0;
		std::vector<SceneStorageIssue> sceneStorageIssues_;
		std::vector<std::filesystem::path> sceneRecoveries_;
		std::vector<std::string> sceneRecoveryLabels_;
		std::string sceneStorageMessage_;
		std::string actorRestorePath_;
		int selectedSceneIssue_ = -1;
		bool confirmActorRemoval_ = false;
		std::vector<std::filesystem::path> sceneRemovalPreview_;
		bool requestSceneStoragePopup_ = false;
		// シーンの検証結果と明示的な修復操作を表示する
		void DrawSceneStoragePopup(const EditorPanelContext& context, AssetDatabase& database);
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
		// 現在ディレクトリ内のモデルを1枚のRenderTextureへまとめて描画する
		void PrepareModelPreviewAtlas(const EditorPanelContext& context, AssetDatabase& database, const ProjectDirectoryNode& node);
		// モデルプレビューAtlas用の一時Worldとスロットを構築する
		void RebuildModelPreviewSlots(AssetDatabase& database, const ProjectDirectoryNode& node,
			const std::vector<const ProjectAssetEntry*>& meshAssets, uint64_t signature);
		// モデルプレビューAtlasを描画する
		bool RenderModelPreviewAtlas(const EditorToolContext& toolContext, EditorToolRenderTexture& atlas);
		// プレビューWorld内のライトへ現在の表示設定を反映する
		void ApplyModelPreviewLightSettings();
		// AssetIDからモデルプレビューの表示情報を取得する
		bool TryGetModelPreviewImage(AssetID assetID, ImTextureID& outTextureID, ImVec2& outUV0, ImVec2& outUV1) const;
		// アセットリストからプレビュー再構築用の署名を作る
		uint64_t BuildModelPreviewSignature(const ProjectDirectoryNode& node,
			const std::vector<const ProjectAssetEntry*>& meshAssets) const;
		// モデル全体を収めるカメラを作るための境界を取得する
		ModelPreviewBounds ComputeModelPreviewBounds(AssetDatabase& database, AssetID meshAssetID) const;
		// モデルの境界に合わせたプレビューカメラを作る
		ManualRenderCameraState BuildModelPreviewCamera(const ModelPreviewBounds& bounds) const;
		// 空白部分の右クリックメニューを描画する
		void DrawDirectoryContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node);
		// フォルダ右クリックメニューを描画する
		void DrawFolderContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node);
		// アセット右クリックメニューを描画する
		void DrawAssetContextMenu(const EditorPanelContext& context, AssetDatabase& database, const ProjectAssetEntry& asset);
		// 新規作成用の名前入力ポップアップを描画する
		void DrawCreateAssetPopup(AssetDatabase& database);
		// アセットリネーム用の名前入力ポップアップを描画する
		void DrawRenameAssetPopup(const EditorPanelContext& context,
			AssetDatabase& database);
		// 削除確認ポップアップを描画する(参照元があれば警告する)
		void DrawDeleteAssetPopup(AssetDatabase& database);
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

