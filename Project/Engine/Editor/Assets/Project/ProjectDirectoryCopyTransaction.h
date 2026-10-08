#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetCopyTransaction.h"
// c++
#include <filesystem>
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	ProjectDirectoryCopyTransaction class
	//	フォルダーのコピーを準備し、所有したファイルだけを取り消す
	//============================================================================
	class ProjectDirectoryCopyTransaction {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ProjectDirectoryCopyTransaction(const std::filesystem::path& target);
		~ProjectDirectoryCopyTransaction();
		ProjectDirectoryCopyTransaction(const ProjectDirectoryCopyTransaction&) = delete;
		ProjectDirectoryCopyTransaction& operator=(const ProjectDirectoryCopyTransaction&) = delete;

		// 専用の作業先を確保する
		bool Begin(std::string& diagnostic);
		// 相対位置にフォルダーを作成する
		bool AddDirectory(const std::filesystem::path& relative, std::string& diagnostic);
		// コピーと編集が成功したファイルだけを作業先へ置く
		bool StageFile(const std::filesystem::path& source, const std::filesystem::path& relative, std::string& diagnostic,
			const AssetCopyPreparation& prepare = {});
		// 既存の公開先を置き換えずフォルダーを公開する
		bool Publish(std::string& diagnostic);
		// 公開したフォルダーの所有を渡す
		void Commit();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 作成したファイルと公開内容
		struct FileEntry {

			std::filesystem::path relative;
			std::string revision;
			StorageFileUtility::FileIdentity identity;
		};
		// 作成したフォルダーと識別情報
		struct DirectoryEntry {

			std::filesystem::path relative;
			StorageFileUtility::FileIdentity identity;
		};

		//--------- variables ----------------------------------------------------

		std::filesystem::path target_;					// 公開先
		std::filesystem::path staging_;					// 専用の作業先
		std::vector<FileEntry> files_;					// コピーした内容
		std::vector<DirectoryEntry> directories_;		// 作成したフォルダー
		StorageFileUtility::FileIdentity rootIdentity_; // 作成したフォルダーの識別情報
		bool published_ = false;						// 公開済み
		bool validated_ = false;						// 公開後の照合完了
		bool committed_ = false;						// 所有移譲済み
		bool preparing_ = false;						// 子ファイルの準備中

		//--------- functions ----------------------------------------------------

		// 作成済みの親フォルダーを処理中に保持する
		std::vector<std::unique_ptr<StorageDirectoryLease>> AcquireParents(const std::filesystem::path& relative) const;
		// 所有した全フォルダーを照合中に保持する
		std::vector<std::unique_ptr<StorageDirectoryLease>> AcquireDirectories(const std::filesystem::path& root) const;
		// 公開前後の全ファイルと所有を照合する
		bool ValidateFiles(const std::filesystem::path& root, std::string& diagnostic) const;
		// 外部変更と未登録ファイルを残して片付ける
		void Rollback() noexcept;
	};
}
