#include "StorageFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>

// Windows
#include <Windows.h>

namespace Engine::StorageFileUtility {

	std::string PathKey(const Path& path) {

		return Algorithm::ToLower(Algorithm::PathToUTF8(std::filesystem::weakly_canonical(path)));
	}

	bool IsInside(const Path& path, const Path& root) {

		if (path.empty() || root.empty()) return false;
		const std::string key = PathKey(path);
		const std::string parent = PathKey(root);
		return key.size() > parent.size() && key.starts_with(parent) &&
			(key[parent.size()] == '/' || key[parent.size()] == '\\');
	}

	std::string FileRevision(const Path& path) {

		if (!std::filesystem::exists(path)) return "missing";
		const std::string hash = ContentHash::FileSHA256(path);
		if (hash.empty()) throw std::runtime_error("ファイルを読み込めません: " + Algorithm::PathToUTF8(path));
		return hash;
	}

	bool WriteBytes(const std::filesystem::path& path, const std::string& serialized) {

		// 内容が同じなら更新時刻を維持する
		std::ifstream currentFile(path, std::ios::binary);
		const std::string current((std::istreambuf_iterator<char>(currentFile)), std::istreambuf_iterator<char>());
		const bool readable = currentFile.is_open() && !currentFile.bad();
		currentFile.close();
		if (readable && current == serialized) {
			return true;
		}

		std::error_code error;
		if (!path.parent_path().empty()) {
			std::filesystem::create_directories(path.parent_path(), error);
			if (error) {
				return false;
			}
		}

		// 既存のtmpやbakには触れず、この保存だけの作業先を作る
		std::filesystem::path temporary = path;
		temporary += L"." + Algorithm::PathFromUTF8(ToString(AssetGUID::New())).native() + L".tmp";
		HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		// 全byteとディスクへの反映が成功するまで元ファイルを保持する
		bool succeeded = true;
		size_t offset = 0;
		while (offset < serialized.size()) {
			DWORD count = static_cast<DWORD>((std::min)(serialized.size() - offset,
				static_cast<size_t>(std::numeric_limits<DWORD>::max())));
			DWORD written = 0;
			if (!WriteFile(file, serialized.data() + offset, count, &written, nullptr) || written == 0) {
				succeeded = false;
				break;
			}
			offset += written;
		}
		succeeded = succeeded && FlushFileBuffers(file);
		succeeded = CloseHandle(file) && succeeded;
		if (succeeded) {
			succeeded = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
		}
		if (!succeeded) {
			std::filesystem::remove(temporary, error);
		}
		return succeeded;
	}

	bool CopyFileAtomically(const Path& source, const Path& target) {

		// 元の保存byteを変換せず復旧する
		std::ifstream file(source, std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (!file.is_open() || file.bad()) return false;
		file.close();
		return WriteBytes(target, bytes);
	}
}
