#pragma once

//============================================================================
//	include
//============================================================================
#include "StorageFileUtility.h"

namespace Engine {

	//============================================================================
	//	StorageDirectoryLease class
	//	子ファイルの処理中に所有フォルダーの差替えを防ぐ
	//============================================================================
	class StorageDirectoryLease {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		StorageDirectoryLease(const StorageFileUtility::Path& path, const StorageFileUtility::FileIdentity& identity);
		~StorageDirectoryLease();
		StorageDirectoryLease(const StorageDirectoryLease&) = delete;
		StorageDirectoryLease& operator=(const StorageDirectoryLease&) = delete;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		void* handle_ = nullptr; // 差替えを防ぐフォルダーhandle
	};
}
