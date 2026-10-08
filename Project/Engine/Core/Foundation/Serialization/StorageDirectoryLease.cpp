#include "StorageDirectoryLease.h"

//============================================================================
//	include
//============================================================================
#include "StorageFileHandleUtility.h"
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <system_error>

// Windows
#include <Windows.h>

Engine::StorageDirectoryLease::StorageDirectoryLease(
	const StorageFileUtility::Path& path, const StorageFileUtility::FileIdentity& identity) {

	// 子ファイルの移動を許可し、フォルダー自身の移動を防ぐ
	const auto nativePath = Algorithm::ToFileSystemPath(path);
	HANDLE handle = CreateFileW(nativePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "保存作業先を保持できません");
	}
	try {
		// Junctionや別フォルダーへ接続しない
		BY_HANDLE_FILE_INFORMATION information{};
		if (!GetFileInformationByHandle(handle, &information)) {
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "保存作業先を確認できません");
		}
		if (!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
			throw std::system_error(
				std::make_error_code(std::errc::invalid_argument), "保存作業先が通常のフォルダーではありません");
		}
		std::error_code error;
		StorageFileUtility::FileIdentity current;
		if (!StorageFileHandleUtility::ReadIdentity(handle, current, error)) {
			throw std::system_error(error, "保存作業先の識別情報を取得できません");
		}
		if (current != identity) {
			throw std::system_error(std::make_error_code(std::errc::state_not_recoverable), "保存作業先の所有が変わっています");
		}
		handle_ = handle;
	} catch (...) {
		CloseHandle(handle);
		throw;
	}
}

Engine::StorageDirectoryLease::~StorageDirectoryLease() {

	// 子ファイルの処理を終えて差替え制限を解除する
	CloseHandle(handle_);
}
