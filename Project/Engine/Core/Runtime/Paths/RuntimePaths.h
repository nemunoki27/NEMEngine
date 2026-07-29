#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Packages/PackageResolver.h>

// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	enum class SceneStorageMode {

		Monolithic,
		ExternalActors,
	};

	//============================================================================
	//	RuntimePaths class
	//	実行時に使用するプロジェクト/エンジンのパスを管理するクラス
	//============================================================================
	class RuntimePaths {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RuntimePaths() = delete;
		~RuntimePaths() = delete;

		// パス情報を再取得
		static void Refresh();

		//--------- accessor -----------------------------------------------------

		// 実行中プロジェクトのルートを取得
		static const std::filesystem::path& GetProjectRoot();
		// ゲームソースのルートを取得
		static const std::filesystem::path& GetGameRoot();
		// NEMEngine/Projectのルートを取得
		static const std::filesystem::path& GetEngineProjectRoot();
		// Engine/Assetsのルートを取得
		static const std::filesystem::path& GetEngineAssetsRoot();
		// ゲームアセットのルートを取得
		static const std::filesystem::path& GetGameAssetsRoot();
		// プロジェクト記述子を取得
		static const std::filesystem::path& GetProjectDescriptorPath();
		// プロジェクトGUIDを取得
		static const std::string& GetProjectGUID();
		// プロジェクト名を取得
		static const std::string& GetProjectName();
		// ゲームシーンの保存形式を取得
		static SceneStorageMode GetSceneStorageMode();
		// 共有プロジェクト設定のルートを取得
		static const std::filesystem::path& GetProjectSettingsRoot();
		// ユーザー固有設定のルートを取得
		static const std::filesystem::path& GetUserSettingsRoot();
		// 再生成可能なインポートキャッシュのルートを取得
		static const std::filesystem::path& GetLibraryRoot();
		// ログや一時ビルド成果物のルートを取得
		static const std::filesystem::path& GetSavedRoot();
		// パッケージmanifestと埋め込みパッケージのルートを取得
		static const std::filesystem::path& GetPackagesRoot();
		// 解決済みパッケージのマウント一覧を取得
		static const std::vector<ResolvedPackage>& GetPackages();
		// パッケージ解決時の問題一覧を取得
		static const std::vector<PackageResolveIssue>& GetPackageIssues();

		// Engine/Assets配下のパスを取得
		static std::filesystem::path GetEngineAssetPath(const std::filesystem::path& relativePath);
		// ProjectSettings配下のパスを取得
		static std::filesystem::path GetProjectSettingsPath(const std::filesystem::path& relativePath);
		// UserSettings配下のパスを取得
		static std::filesystem::path GetUserSettingsPath(const std::filesystem::path& relativePath);
		// Library配下のパスを取得
		static std::filesystem::path GetLibraryPath(const std::filesystem::path& relativePath);
		// Saved配下のパスを取得
		static std::filesystem::path GetSavedPath(const std::filesystem::path& relativePath);
		// 仮想URIから実ファイルパスを取得
		static std::filesystem::path ResolveVirtualPath(std::string_view virtualPath);
		// 実ファイルパスから仮想URIを取得
		static std::string ToVirtualPath(const std::filesystem::path& fullPath);
		// 論理アセットパスから実ファイルパスを取得
		static std::filesystem::path ResolveAssetPath(const std::filesystem::path& assetPath);
		static std::filesystem::path ResolveAssetPath(const std::string& assetPath);
		static std::filesystem::path ResolveAssetPath(const char* assetPath);
		// 実ファイルパスから論理アセットパスを取得
		static std::string ToAssetPath(const std::filesystem::path& fullPath);

		struct PathState {

			std::filesystem::path projectRoot;
			std::filesystem::path gameRoot;
			std::filesystem::path engineProjectRoot;
			std::filesystem::path engineAssetsRoot;
			std::filesystem::path gameAssetsRoot;
			std::filesystem::path projectDescriptorPath;
			std::filesystem::path projectSettingsRoot;
			std::filesystem::path userSettingsRoot;
			std::filesystem::path libraryRoot;
			std::filesystem::path savedRoot;
			std::filesystem::path packagesRoot;
			std::string projectGUID;
			std::string projectName;
			SceneStorageMode sceneStorageMode =
				SceneStorageMode::ExternalActors;
			std::vector<ResolvedPackage> packages;
			std::vector<PackageResolveIssue> packageIssues;
		};
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// パス情報を取得
		static const PathState& GetState();
		// パス情報を構築
		static PathState BuildState();
	};
} // Engine

