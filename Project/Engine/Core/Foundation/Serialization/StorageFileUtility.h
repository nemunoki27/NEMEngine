#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <string>

namespace Engine::StorageFileUtility {

	using Path = std::filesystem::path;

	// 保存先の比較と復旧前後の照合
	std::string PathKey(const Path& path);
	bool IsInside(const Path& path, const Path& root);
	std::string FileRevision(const Path& path);

	// 全byteを書き終えてから保存先を置き換える
	bool WriteBytes(const Path& path, const std::string& serialized);
	bool CopyFileAtomically(const Path& source, const Path& target);
}
