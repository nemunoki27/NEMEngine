#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>

// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	class ProjectDirectoryCopyTransaction;

	//============================================================================
	//	ProjectModelImportPlan class
	//	モデルと参照ファイルの配置と保存内容を準備する
	//============================================================================
	class ProjectModelImportPlan {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 通常読込と文書参照から必要なファイルを揃える
		bool Prepare(const std::filesystem::path& model, std::string& diagnostic);
		// 更新された入力を拒否し、作業先へ全ファイルを準備する
		bool Stage(ProjectDirectoryCopyTransaction& transaction, std::string& diagnostic,
			const std::filesystem::path& relativeRoot = {}) const;
		// 公開先のMaterialが取り込み元と同じ画像を解決するか確認する
		bool Verify(const std::filesystem::path& root, std::string& diagnostic) const;

		//--------- accessor -----------------------------------------------------

		std::filesystem::path GetMainRelativePath() const { return model_.filename(); }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 読込時の内容とコピー先の相対位置
		struct FileEntry {

			std::filesystem::path source;
			std::filesystem::path relative;
			std::string key;
			std::string revision;
			std::string bytes;
		};

		//--------- variables ----------------------------------------------------

		std::filesystem::path model_;
		std::vector<FileEntry> files_;
		TextureAssetResolver textures_;
		std::vector<std::vector<std::filesystem::path>> materialTextures_;
		bool prepared_ = false;

		//--------- functions ----------------------------------------------------

		// 同じ実ファイルを重複させず内容を保持する
		bool AddFile(const std::filesystem::path& path, std::string& diagnostic);
		// 元ファイルの配置計画を探す
		const FileEntry* FindFile(const std::filesystem::path& path) const;
		// 文書内の参照を元の実ファイルへ解決する
		std::filesystem::path ResolveReference(const std::filesystem::path& document, const std::string& reference) const;
		// 文書から未使用のBufferや画像も収集する
		bool CollectReferences(std::string& diagnostic);
		// 同名ファイルを別の関連フォルダーへ分ける
		void AssignPaths();
		// 文書の参照を取り込み後の相対位置へ更新する
		bool RewriteReferences(std::string& diagnostic);
		// 準備後に入力ファイルが変更されていないか確認する
		bool ValidateSources(std::string& diagnostic) const;
	};
}
