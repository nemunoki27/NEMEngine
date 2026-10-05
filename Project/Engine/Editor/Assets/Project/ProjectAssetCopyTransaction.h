#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

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
		// 既存ファイルを置き換えず公開する
		bool Publish(std::string& diagnostic);
		// 公開したファイルの所有を渡す
		void Commit();

		//--------- accessor -----------------------------------------------------

		// 作業中のファイルパスを取得する
		std::filesystem::path GetStagedPath(size_t index) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// コピー元と公開先
		struct CopyEntry {

			std::filesystem::path source;
			std::filesystem::path target;
		};

		//--------- variables ----------------------------------------------------

		// 公開先と専用の作業先
		std::filesystem::path targetDirectory_;
		std::filesystem::path stagingDirectory_;
		// 最初の要素を最後に公開する
		std::vector<CopyEntry> entries_;
		// 末尾から公開したファイル数
		size_t publishedCount_ = 0;
		// 全ファイルの準備完了
		bool staged_ = false;
		// 公開先への所有移譲
		bool committed_ = false;

		//--------- functions ----------------------------------------------------

		// この操作が作ったファイルだけを取り消す
		void Rollback();
		// 専用の作業先を片付ける
		void RemoveStaging();
	};
}
