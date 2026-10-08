#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/StorageFilePublication.h>
// c++
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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
		// 移動前の内容を保持して文書の変更を準備する
		bool Prepare(size_t index, const std::function<bool(std::string&)>& prepare, std::string& diagnostic);
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
			StorageFileUtility::FileIdentity identity;			 // 移動前の対象識別
			std::string revision;								 // 編集前の内容
			std::optional<std::string> prepared;				 // 移動後に適用する内容
			std::unique_ptr<StorageFilePublication> publication; // 編集前のファイルを保持する公開操作
			bool moved = false;									 // 復元が必要な移動
		};

		//--------- variables ----------------------------------------------------

		// 移動前に確定する計画
		std::vector<MoveEntry> entries_;
		// 未復元の適用件数
		size_t movedCount_ = 0;
		// 計画の変更と再適用を禁止する
		bool started_ = false;
		// 編集callback中は計画と実行状態を変更しない
		bool preparing_ = false;
		// 移動と文書の公開が全て完了した状態
		bool executed_ = false;
		// 成功した移動の確定
		bool committed_ = false;
		// 全ての復元が完了した状態
		bool rolledBack_ = false;
		// 復元開始後の確定を禁止する
		bool rollbackStarted_ = false;

		//--------- functions ----------------------------------------------------

		// 移動した文書へ準備済みの内容を公開する
		bool PublishPrepared(MoveEntry& entry, std::string& diagnostic);
		// 公開した文書を取り消して編集前の所有へ戻す
		bool RestorePrepared(MoveEntry& entry);
		// 公開記録と一致する作業ファイルだけを回収する
		void CleanupPrepared(MoveEntry& entry) noexcept;
	};
}
