#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetIndex.h>

// c++
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Engine {

	//============================================================================
	//	ProjectAssetFileKind enum class
	//============================================================================
	enum class ProjectAssetFileKind :
		uint8_t {

		Folder,
		Text,
		Script,
		Scene,
		Prefab,
		Material,
		AnimationClip,
		Shader,
		RenderPipeline,
	};

	//============================================================================
	//	ProjectAssetFileResult structure
	//============================================================================
	struct ProjectAssetFileResult {

		bool success = false;
		bool isDirectory = false;

		std::string assetPath;
		std::filesystem::path fullPath;
		std::string message;
	};

	//============================================================================
	//	ProjectAssetFileUtility class
	//	プロジェクトパネルで使用するファイル操作ユーティリティ
	//============================================================================
	class ProjectAssetFileUtility {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ProjectAssetFileUtility() = delete;
		~ProjectAssetFileUtility() = delete;

		// 作成メニューに表示する名前を取得する
		static const char* GetCreateMenuLabel(ProjectAssetFileKind kind);
		// 新規作成時のデフォルト名を取得する
		static const char* GetDefaultName(ProjectAssetFileKind kind);
		// リネーム入力で編集できるファイル名部分を取得する
		static std::string GetEditableAssetName(const ProjectAssetEntry& asset);
		// リネーム時に保護するファイル名サフィックスを取得する
		static std::string GetProtectedAssetSuffix(const ProjectAssetEntry& asset);

		// 指定ディレクトリにアセットまたはフォルダを作成する
		static ProjectAssetFileResult Create(ProjectAssetSource source,
			const std::string& directoryVirtualPath, ProjectAssetFileKind kind, const std::string& requestedName);
		// 指定アセットを同じディレクトリに複製する
		static ProjectAssetFileResult DuplicateAsset(const ProjectAssetEntry& asset);
		// 指定アセットを別ディレクトリへコピーする、コピペ操作で使う
		static ProjectAssetFileResult CopyAsset(const ProjectAssetEntry& asset, ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath);
		// 外部エクスプローラーからドロップされたファイルを指定ディレクトリへ取り込む
		static ProjectAssetFileResult ImportExternalFile(ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalFilePath);
		// 外部エクスプローラーからドロップされたフォルダを中身ごと指定ディレクトリへ取り込む
		static ProjectAssetFileResult ImportExternalDirectory(ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalDirectoryPath);
		// 指定アセットの保護サフィックスより前の名前を変更する
		static ProjectAssetFileResult RenameAsset(const ProjectAssetEntry& asset, const std::string& requestedName);
		// 指定ディレクトリの名前を変更する
		static ProjectAssetFileResult RenameDirectory(ProjectAssetSource source,
			const std::string& directoryVirtualPath, const std::string& requestedName);
		// 指定ディレクトリを同じ階層に複製する
		static ProjectAssetFileResult DuplicateDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath);
		// 指定アセットを削除する
		static ProjectAssetFileResult DeleteAsset(const ProjectAssetEntry& asset);
		// 指定ディレクトリを削除する
		static ProjectAssetFileResult DeleteDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath);
		// 指定アセットを別ディレクトリへ移動する
		static ProjectAssetFileResult MoveAsset(const ProjectAssetEntry& asset, ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath);
		// 指定ディレクトリを別ディレクトリへ移動する
		static ProjectAssetFileResult MoveDirectory(ProjectAssetSource source, const std::string& sourceDirectoryVirtualPath,
			const std::string& targetDirectoryVirtualPath);

		//--------- accessor -----------------------------------------------------

		// ソースに対応する実ファイル上のルートを取得する
		static std::filesystem::path GetSourceRoot(ProjectAssetSource source);
		// 仮想ディレクトリパスから実ディレクトリパスを取得する
		static std::filesystem::path ResolveVirtualDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 仮想ルート名を取得する
		static const char* GetSourceVirtualRoot(ProjectAssetSource source);
		// ファイル作成用の拡張子を取得する
		static const char* GetFileSuffix(ProjectAssetFileKind kind);
		// 作成するファイルの中身を構築する
		static std::string BuildFileContent(ProjectAssetFileKind kind, const std::string& assetName);
		// 作成したテキストファイルを書き出す
		static bool WriteTextFile(const std::filesystem::path& path, const std::string& content);
		// ゲームスクリプトのルート名前空間をcsprojから取得する
		static std::string LoadGameScriptRootNamespace();

		//--------- path helpers -------------------------------------------------

		// ファイル名として使えない文字を除去・置換する
		static std::string SanitizeFileName(std::string text);
		// ファイル名からC#クラス名として有効な文字列を生成する
		static std::string MakeCSharpClassName(const std::string& fileName);
		// ファイル名をベース名とサフィックス(複合拡張子含む)に分割する
		static std::pair<std::string, std::string> SplitAssetFileName(const std::filesystem::path& path);
		// 既存ファイルと衝突しない一意なパスを作る
		static std::filesystem::path MakeUniquePath(const std::filesystem::path& preferredPath);
		// 末尾のアセット拡張子を取り除く
		static std::string RemoveTypedSuffix(std::string name, const char* suffix);
		// 親階層への移動(..)を含まない安全な相対パスかを判定する
		static bool IsSafeRelativePath(const std::filesystem::path& path);
		// 実パスからAssets/...形式の仮想パスへ変換する
		static std::string ToAssetPath(const std::filesystem::path& fullPath);
		// あるパスが指定の親パスと同一または配下かを判定する
		static bool IsSameOrChildPath(const std::filesystem::path& path, const std::filesystem::path& parent);
		// アセットパスに対応する.metaファイルのパスを作る
		static std::filesystem::path MakeMetaPath(const std::filesystem::path& path);

		//--------- json patch ---------------------------------------------------

		// JSONアセット内部の名前(必要ならGUID)をファイル名へ合わせて更新する
		static void PatchJsonAssetName(const std::filesystem::path& path, AssetType type, bool resetGuid);
		// 複製アセットの名前を更新しGUIDをリセットする
		static void PatchDuplicatedJsonAsset(const std::filesystem::path& path, AssetType type);
		// リネームしたアセットの名前のみを更新する
		static void PatchRenamedJsonAsset(const std::filesystem::path& path, AssetType type);
		// 複製ディレクトリ内の全JSONアセットを一括修正する
		static void PatchDuplicatedDirectoryAssets(const std::filesystem::path& duplicatedDirectory);
		// コピー対象から除外すべきファイル(.meta等)かを判定する
		static bool ShouldSkipCopyFile(const std::filesystem::path& path);
		// アセットに随行するサイドカーファイルのパス一覧を作る
		static std::vector<std::filesystem::path> BuildAssetSidecarPaths(
			const ProjectAssetEntry& asset, const std::filesystem::path& assetPath);
	};
} // Engine

