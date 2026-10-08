#include "StorageFileHandleUtility.h"

//============================================================================
//	include
//============================================================================
// c++
#include <cstring>

// Windows
#include <Windows.h>

bool Engine::StorageFileHandleUtility::ReadIdentity(
	void* handle, StorageFileUtility::FileIdentity& identity, std::error_code& error) noexcept {

	// パスを引き直さず取得済みの対象を照合する
	FILE_ID_INFO info{};
	if (!GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info))) {
		error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
		return false;
	}
	identity.volumeID = info.VolumeSerialNumber;
	std::memcpy(identity.fileID.data(), info.FileId.Identifier, identity.fileID.size());
	error.clear();
	return true;
}
