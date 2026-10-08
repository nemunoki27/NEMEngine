#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	StoragePreparationDirectory class
	//	保存準備で所有したファイルだけを回収する
	//============================================================================
	class StoragePreparationDirectory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit StoragePreparationDirectory(const StorageFileUtility::Path& path);
		~StoragePreparationDirectory();
		StoragePreparationDirectory(const StoragePreparationDirectory&) = delete;
		StoragePreparationDirectory& operator=(const StoragePreparationDirectory&) = delete;

		// 作成前に回収対象と予定内容を登録する
		void Track(const StorageFileUtility::Path& path, const std::string& revision);
		// 作成後のファイル識別情報を保持する
		void Capture(const StorageFileUtility::Path& path);
		// 作成時の所有と一致するファイルだけを回収対象へ登録する
		void Capture(const StorageFileUtility::Path& path, const StorageFileUtility::FileIdentity& identity);
		// 所有と内容が一致する準備ファイルだけを非置換で公開する
		bool PublishFile(const StorageFileUtility::Path& path, const StorageFileUtility::Path& target, std::error_code& error);
		// 復旧記録へ回収対象の所有を渡す
		void Publish();

		//--------- accessor -----------------------------------------------------

		// 最初に所有したフォルダーの識別情報
		const StorageFileUtility::FileIdentity& GetIdentity() const { return identity_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 回収する内容と所有を一組で保持する
		struct FileEntry {

			StorageFileUtility::Path path;			   // 作成先
			std::string revision;					   // 予定内容
			StorageFileUtility::FileIdentity identity; // 所有するファイル
			bool captured = false;					   // 識別情報の取得完了
		};

		//--------- variables ----------------------------------------------------

		StorageFileUtility::Path path_;				// 保存準備先
		StorageFileUtility::FileIdentity identity_; // 所有するフォルダー
		std::vector<FileEntry> files_;				// 回収対象
		bool published_ = false;					// 所有移譲の完了

		//--------- functions ----------------------------------------------------

		// 作成済みの所有を照合して回収対象を確定する
		void CaptureImpl(const StorageFileUtility::Path& path, const StorageFileUtility::FileIdentity* identity);
	};
}
