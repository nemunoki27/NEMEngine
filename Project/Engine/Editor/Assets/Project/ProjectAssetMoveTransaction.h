#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	ProjectAssetMoveTransaction class
	//	移動計画を保持し、失敗時に適用済みの移動を戻す
	//============================================================================
	class ProjectAssetMoveTransaction {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProjectAssetMoveTransaction() = default;
		~ProjectAssetMoveTransaction();
		ProjectAssetMoveTransaction(const ProjectAssetMoveTransaction&) = delete;
		ProjectAssetMoveTransaction& operator=(const ProjectAssetMoveTransaction&) = delete;

		// 移動開始前に元と先の組を登録する
		bool Add(const std::filesystem::path& source, const std::filesystem::path& target);
		// 移動先を上書きせず計画を適用する
		bool Execute(std::string& diagnostic);
		// 全移動の成功後に確定する
		void Commit();
		// 適用済みの移動を逆順に戻す
		bool Rollback() noexcept;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 移動元と移動先
		struct MoveEntry {

			std::filesystem::path source;
			std::filesystem::path target;
		};

		//--------- variables ----------------------------------------------------

		// 移動前に確定する計画
		std::vector<MoveEntry> entries_;
		// 先頭から適用した件数
		size_t movedCount_ = 0;
		// 計画の変更と再適用を禁止する
		bool started_ = false;
		// 成功した移動の確定
		bool committed_ = false;
		// 復元の再実行を禁止する
		bool rolledBack_ = false;
	};
}
