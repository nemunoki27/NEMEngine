#pragma once

//============================================================================
//	include
//============================================================================
#include "StorageFileUtility.h"

// c++
#include <memory>
#include <optional>
#include <string>

namespace Engine {

	class StoragePreparationDirectory;

	// 保存先と同じボリュームに置く公開用の所有記録
	struct StorageFilePublicationRecord {

		std::string stageName;							 // 専用フォルダーの名前
		StorageFileUtility::FileIdentity stageIdentity;	 // 専用フォルダーの所有
		std::string before = "missing";					 // 変更前の内容
		StorageFileUtility::FileIdentity beforeIdentity; // 変更前のファイルの所有
		std::string after = "missing";					 // 変更後の内容
		StorageFileUtility::FileIdentity afterIdentity;	 // 準備したファイルの所有
	};

	//============================================================================
	//	StorageFilePublication class
	//	記録済みの所有と内容を照合してファイルを公開する
	//============================================================================
	class StorageFilePublication {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		StorageFilePublication(
			const StorageFileUtility::Path& target, const std::string& before, const std::optional<std::string>& bytes);
		~StorageFilePublication();
		StorageFilePublication(const StorageFilePublication&) = delete;
		StorageFilePublication& operator=(const StorageFilePublication&) = delete;
		StorageFilePublication(StorageFilePublication&& other) noexcept;
		StorageFilePublication& operator=(StorageFilePublication&& other) noexcept;

		// 記録を保存できた後に回収の所有を渡す
		void Persist();
		// 変更前のファイルを専用フォルダーへ退避する
		void Retire();
		// 準備したファイルを空いている保存先へ公開する
		void Publish();
		// 中断時に退避した元ファイルを空いている保存先へ戻す
		void RestoreRetired();
		// 保存済みの記録から操作を再開する
		static StorageFilePublication Resume(
			const StorageFileUtility::Path& target, const StorageFilePublicationRecord& record);
		// 記録と一致する準備ファイルだけを回収する
		static bool Cleanup(
			const StorageFileUtility::Path& target, const StorageFilePublicationRecord& record, std::error_code& error);

		//--------- accessor -----------------------------------------------------

		// 記録保存まで変更しない公開用の所有情報
		const StorageFilePublicationRecord& GetRecord() const { return record_; }
		// 中断時に元ファイルが所有した退避先に残っているか
		bool HasRetiredOriginal() const;
		// 公開したファイルの所有と内容が一致するか
		bool IsPublished() const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		StorageFileUtility::Path target_;						   // 公開先
		StorageFilePublicationRecord record_;					   // 公開する内容と所有
		std::unique_ptr<StoragePreparationDirectory> preparation_; // 未記録の準備ファイル
		bool persisted_ = false;								   // 回収の所有を記録へ移譲済み

		//--------- functions ----------------------------------------------------

		// 保存済みの記録から構築する
		StorageFilePublication() = default;
		// 記録前に公開先を変更しない
		void RequirePersisted() const;
		// 保存先から専用フォルダーのパスを導く
		StorageFileUtility::Path GetStagePath() const;
	};
}
