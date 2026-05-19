#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <filesystem>
#include <string>

namespace Engine {

	//============================================================================
	//	RuntimePaths class
	//	実行時に使用するプロジェクト/エンジンのパスを管理するクラス
	//============================================================================
	class RuntimePaths {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

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
		// Engine/Libraryのルートを取得
		static const std::filesystem::path& GetEngineLibraryRoot();

		// Engine/Assets配下のパスを取得
		static std::filesystem::path GetEngineAssetPath(const std::filesystem::path& relativePath);
		// 論理アセットパスから実ファイルパスを取得
		static std::filesystem::path ResolveAssetPath(const std::filesystem::path& assetPath);
		// 実ファイルパスから論理アセットパスを取得
		static std::string ToAssetPath(const std::filesystem::path& fullPath);

		struct PathState {

			std::filesystem::path projectRoot;
			std::filesystem::path gameRoot;
			std::filesystem::path engineProjectRoot;
			std::filesystem::path engineAssetsRoot;
			std::filesystem::path engineLibraryRoot;
		};
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// パス情報を取得
		static const PathState& GetState();
		// パス情報を構築
		static PathState BuildState();
	};
} // Engine
