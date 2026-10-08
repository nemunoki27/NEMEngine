#pragma once

//============================================================================
//	include
//============================================================================
#include "StorageFileUtility.h"

namespace Engine::StorageFileHandleUtility {

	// 取得済みhandleから所有を読み取り、開き直しを避ける
	bool ReadIdentity(void* handle, StorageFileUtility::FileIdentity& identity, std::error_code& error) noexcept;
}
