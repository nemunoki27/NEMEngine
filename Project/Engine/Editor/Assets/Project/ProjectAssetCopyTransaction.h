#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
// c++
#include <cstddef>
#include <memory>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Engine {

	class StorageDirectoryLease;

	// 作成時の所有と公開予定の内容
	struct ProjectAssetCopySnapshot {

		StorageFileUtility::FileIdentity identity; // 作成したファイル
		std::string revision;					   // 公開する内容
	};

	// 作業byteを編集し、ファイルの公開は操作側へ任せる
	using AssetCopyPreparation = std::function<bool(const std::filesystem::path&, std::string&)>;

	//============================================================================
	//	ProjectAssetCopyTransaction class
	//	コピー途中のファイルを保持し、失敗時に取り消す
	//============================================================================
	class ProjectAssetCopyTransaction {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ProjectAssetCopyTransaction(const std::filesystem::path& targetDirectory);
		~ProjectAssetCopyTransaction();
		ProjectAssetCopyTransaction(const ProjectAssetCopyTransaction&) = delete;
		ProjectAssetCopyTransaction& operator=(const ProjectAssetCopyTransaction&) = delete;

		// 同じフォルダーへ公開するファイルを登録する
		bool Add(const std::filesystem::path& source, const std::filesystem::path& target);
		// 全ファイルを専用の作業先へコピーする
		bool Stage(std::string& diagnostic);
		// 作業byteの編集に成功してから差し替える
		bool Prepare(size_t index, const AssetCopyPreparation& prepare, std::string& diagnostic);
		// 既存ファイルを置き換えず公開する
		bool Publish(std::string& diagnostic);
		// 公開したファイルの所有を渡す
		void Commit();

		//--------- accessor -----------------------------------------------------

		// 作業中のファイルパスを取得する
		std::filesystem::path GetStagedPath(size_t index) const;
		// 準備した所有と内容を値で取得する
		ProjectAssetCopySnapshot GetStagedSnapshot(size_t index) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// コピー元と公開先
		struct CopyEntry {

			std::filesystem::path source;
			std::filesystem::path target;
			std::string revision;					   // 公開する内容の照合値
			StorageFileUtility::FileIdentity identity; // 所有するファイルの識別情報
		};

		//--------- variables ----------------------------------------------------

		// 公開先と専用の作業先
		std::filesystem::path targetDirectory_;
		std::filesystem::path stagingDirectory_;
		StorageFileUtility::FileIdentity stagingIdentity_; // 作業先の識別情報
		// 最初の要素を最後に公開する
		std::vector<CopyEntry> entries_;
		// 末尾から公開したファイル数
		size_t publishedCount_ = 0;
		// 全ファイルの準備完了
		bool staged_ = false;
		// 編集callbackからの再入を止める
		bool preparing_ = false;
		// 公開先への所有移譲
		bool committed_ = false;

		//--------- functions ----------------------------------------------------

		// 作業先と予定内容の所有を照合する
		bool MatchesStaged(size_t index, std::string& diagnostic) const;
		// 作成時の所有とコピーした内容を照合する
		bool CaptureEntry(
			size_t index, const StorageFileUtility::FileIdentity& identity, std::string revision, std::string& diagnostic);
		// 作業中のフォルダーの差替えを防ぐ
		std::unique_ptr<StorageDirectoryLease> AcquireStaging(std::string& diagnostic) const;
		// この操作が作ったファイルだけを取り消す
		void Rollback() noexcept;
		// 専用の作業先を片付ける
		void RemoveStaging() noexcept;
	};
}
