#include "StorageFileUtility.h"

//============================================================================
//	include
//============================================================================
#include "StorageFileHandleUtility.h"
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <fstream>
#include <functional>
#include <span>
#include <iterator>
#include <limits>
#include <memory>
#include <cstddef>
#include <cstring>
#include <vector>

// Windows
#include <Windows.h>

namespace {

	// 開いたファイルを処理終了時に閉じる
	void CloseFile(void* handle) noexcept {

		CloseHandle(handle);
	}

	// 未公開の作成物を例外時も同じhandleから削除する
	void DiscardFile(void* handle) noexcept {

		FILE_DISPOSITION_INFO disposition{TRUE};
		SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition));
		CloseHandle(handle);
	}

	using FileHandle = std::unique_ptr<void, decltype(&CloseFile)>;

	// 取得済みのファイルを移動先へ公開する
	bool RenameHandle(
		HANDLE handle, const Engine::StorageFileUtility::Path& target, bool replace, std::error_code& error) noexcept {

		try {
			const auto name = Engine::Algorithm::ToFileSystemPath(target).native();
			const size_t nameBytes = name.size() * sizeof(wchar_t);
			const size_t bytes = offsetof(FILE_RENAME_INFO, FileName) + nameBytes + sizeof(wchar_t);
			if (bytes > std::numeric_limits<DWORD>::max()) {
				error = std::make_error_code(std::errc::filename_too_long);
				return false;
			}
			std::vector<uint64_t> storage((bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t));
			auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
			rename->ReplaceIfExists = replace ? TRUE : FALSE;
			rename->RootDirectory = nullptr;
			rename->FileNameLength = static_cast<DWORD>(nameBytes);
			std::memcpy(rename->FileName, name.c_str(), nameBytes + sizeof(wchar_t));
			if (!SetFileInformationByHandle(handle, FileRenameInfo, rename, static_cast<DWORD>(bytes))) {
				error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
				return false;
			}
			error.clear();
			return true;
		} catch (...) {
			error = std::make_error_code(std::errc::io_error);
			return false;
		}
	}

	// 読込中の対象を引き直さず内容を照合する
	bool MatchesHandleRevision(HANDLE handle, const std::string& revision, std::error_code& error) {

		const std::string current = Engine::ContentHash::ReadSHA256([&](std::span<uint8_t> buffer, size_t& count) {
			DWORD read = 0;
			if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
				error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
				return false;
			}
			count = read;
			return true;
		});
		if (current != revision) {
			if (!error) {
				error = std::make_error_code(std::errc::state_not_recoverable);
			}
			return false;
		}
		return true;
	}

	bool RemoveMatching(const Engine::StorageFileUtility::Path& path, const std::string* revision,
		const Engine::StorageFileUtility::FileIdentity* identity, std::error_code& error) {

		error.clear();
		if (revision && (revision->empty() || *revision == "missing")) {
			error = std::make_error_code(std::errc::invalid_argument);
			return false;
		}
		// 照合から削除まで書込と置換を防ぐ
		const auto nativePath = Engine::Algorithm::ToFileSystemPath(path);
		HANDLE rawHandle = CreateFileW(nativePath.c_str(), (revision ? GENERIC_READ : FILE_READ_ATTRIBUTES) | DELETE, 0, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (rawHandle == INVALID_HANDLE_VALUE) {
			const DWORD code = GetLastError();
			if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
				return true;
			}
			error = std::error_code(static_cast<int>(code), std::system_category());
			return false;
		}
		const FileHandle handle(rawHandle, CloseFile);
		// 取得済みの対象で所有を照合
		if (identity) {
			Engine::StorageFileUtility::FileIdentity currentIdentity;
			if (!Engine::StorageFileHandleUtility::ReadIdentity(handle.get(), currentIdentity, error)) {
				return false;
			}
			if (currentIdentity != *identity) {
				error = std::make_error_code(std::errc::state_not_recoverable);
				return false;
			}
		}
		// 取得済みの対象で内容を照合
		if (revision && !MatchesHandleRevision(handle.get(), *revision, error)) {
			return false;
		}
		FILE_DISPOSITION_INFO disposition{TRUE};
		if (!SetFileInformationByHandle(handle.get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
			error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
			return false;
		}
		return true;
	}

	bool MoveMatching(const Engine::StorageFileUtility::Path& source, const Engine::StorageFileUtility::Path& target,
		const Engine::StorageFileUtility::FileIdentity& identity, const std::string* revision,
		std::error_code& error) noexcept {

		try {
			error.clear();
			if (revision && (revision->empty() || *revision == "missing")) {
				error = std::make_error_code(std::errc::invalid_argument);
				return false;
			}
			// 照合から移動まで置換と外部の書込を防ぐ
			const auto nativeSource = Engine::Algorithm::ToFileSystemPath(source);
			const HANDLE rawHandle = CreateFileW(nativeSource.c_str(), (revision ? GENERIC_READ : FILE_READ_ATTRIBUTES) | DELETE, 0,
				nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (rawHandle == INVALID_HANDLE_VALUE) {
				error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
				return false;
			}
			const FileHandle handle(rawHandle, CloseFile);
			Engine::StorageFileUtility::FileIdentity current;
			if (!Engine::StorageFileHandleUtility::ReadIdentity(handle.get(), current, error)) {
				return false;
			}
			if (current != identity) {
				error = std::make_error_code(std::errc::state_not_recoverable);
				return false;
			}
			if (revision && !MatchesHandleRevision(handle.get(), *revision, error)) {
				return false;
			}
			// 取得済みのファイルを移動先の空きにだけ公開
			return RenameHandle(handle.get(), target, false, error);
		} catch (...) {
			error = std::make_error_code(std::errc::io_error);
			return false;
		}
	}

	// 取得済みhandleへ全byteを書き込む
	bool WriteBuffer(HANDLE handle, std::span<const uint8_t> bytes) {

		size_t offset = 0;
		while (offset < bytes.size()) {
			const DWORD count =
				static_cast<DWORD>((std::min)(bytes.size() - offset, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
			DWORD written = 0;
			if (!WriteFile(handle, bytes.data() + offset, count, &written, nullptr) || written == 0) {
				return false;
			}
			offset += written;
		}
		return true;
	}

	// 作成時のhandleを保ったまま一時ファイルを公開する
	bool WriteTemporary(const std::filesystem::path& path, const std::function<bool(HANDLE)>& write, bool replace,
		Engine::StorageFileUtility::FileIdentity* identity) {

		using namespace Engine;
		// 一時ファイルの追加分も長いパスとしてOSへ渡す
		const auto nativePath = Algorithm::ToFileSystemPath(path);
		std::error_code error;
		if (!nativePath.parent_path().empty()) {
			std::filesystem::create_directories(nativePath.parent_path(), error);
			if (error) {
				return false;
			}
		}
		// この保存で作成した対象だけを所有する
		std::filesystem::path temporary = nativePath;
		temporary += L"." + Algorithm::PathFromUTF8(ToString(AssetGUID::New())).native() + L".tmp";
		const HANDLE rawHandle = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE | (identity ? FILE_READ_ATTRIBUTES : 0),
			0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (rawHandle == INVALID_HANDLE_VALUE) {
			return false;
		}
		FileHandle file(rawHandle, DiscardFile);
		bool succeeded = write(file.get()) && FlushFileBuffers(file.get());
		if (succeeded) {
			succeeded = RenameHandle(file.get(), nativePath, replace, error);
		}
		if (succeeded && identity) {
			succeeded = StorageFileHandleUtility::ReadIdentity(file.get(), *identity, error);
		}
		return succeeded && CloseHandle(file.release());
	}

	// 同じ内容を保ち、必要な変更だけ一時ファイルから公開する
	bool WriteBytesImpl(const std::filesystem::path& path, const std::string& serialized, bool replace,
		Engine::StorageFileUtility::FileIdentity* identity) {

		if (replace) {
			std::ifstream file(Engine::Algorithm::ToFileSystemPath(path), std::ios::binary);
			const std::string current{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
			const bool readable = file.is_open() && !file.bad();
			file.close();
			if (readable && current == serialized) {
				return true;
			}
		}
		const auto bytes = std::span(reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size());
		return WriteTemporary(path, [&](HANDLE handle) { return WriteBuffer(handle, bytes); }, replace, identity);
	}

}

namespace Engine::StorageFileUtility {

	bool RemoveIfRevision(const Path& path, const std::string& revision, std::error_code& error) {

		return RemoveMatching(path, &revision, nullptr, error);
	}

	bool RemoveIfRevision(const Path& path, const std::string& revision, const FileIdentity& identity, std::error_code& error) {

		return RemoveMatching(path, &revision, &identity, error);
	}

	bool RemoveIfIdentity(const Path& path, const FileIdentity& identity, std::error_code& error) {

		return RemoveMatching(path, nullptr, &identity, error);
	}

	bool MoveWithoutReplacement(const Path& source, const Path& target, std::error_code& error) noexcept {

		// 事前確認の後に作られた移動先も保護する
		try {
			const auto nativeSource = Algorithm::ToFileSystemPath(source);
			const auto nativeTarget = Algorithm::ToFileSystemPath(target);
			if (!MoveFileExW(nativeSource.c_str(), nativeTarget.c_str(), MOVEFILE_WRITE_THROUGH)) {
				error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
				return false;
			}
			error.clear();
			return true;
		} catch (...) {
			error = std::make_error_code(std::errc::io_error);
			return false;
		}
	}

	bool ReadIdentity(const Path& path, FileIdentity& identity, std::error_code& error) noexcept {

		try {
			const auto nativePath = Algorithm::ToFileSystemPath(path);
			const HANDLE rawHandle = CreateFileW(nativePath.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (rawHandle == INVALID_HANDLE_VALUE) {
				error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
				return false;
			}
			const FileHandle handle(rawHandle, CloseFile);
			return Engine::StorageFileHandleUtility::ReadIdentity(handle.get(), identity, error);
		} catch (...) {
			error = std::make_error_code(std::errc::io_error);
			return false;
		}
	}

	bool MoveIfIdentity(const Path& source, const Path& target, const FileIdentity& identity, std::error_code& error) noexcept {

		return MoveMatching(source, target, identity, nullptr, error);
	}

	bool MoveIfRevision(const Path& source, const Path& target, const std::string& revision, const FileIdentity& identity,
		std::error_code& error) noexcept {

		return MoveMatching(source, target, identity, &revision, error);
	}

	std::string PathKey(const Path& path) {

		return Algorithm::ToLower(Algorithm::PathToUTF8(std::filesystem::weakly_canonical(path)));
	}

	bool IsInside(const Path& path, const Path& root) {

		if (path.empty() || root.empty()) {
			return false;
		}
		const std::string key = PathKey(path);
		const std::string parent = PathKey(root);
		return key.size() > parent.size() && key.starts_with(parent) &&
			   (key[parent.size()] == '/' || key[parent.size()] == '\\');
	}

	std::string FileRevision(const Path& path) {

		if (!std::filesystem::exists(Algorithm::ToFileSystemPath(path))) {
			return "missing";
		}
		const std::string hash = ContentHash::FileSHA256(path);
		if (hash.empty()) {
			throw std::runtime_error("ファイルを読み込めません: " + Algorithm::PathToUTF8(path));
		}
		return hash;
	}

	bool WriteBytes(const std::filesystem::path& path, const std::string& serialized) {

		return WriteBytesImpl(path, serialized, true, nullptr);
	}

	bool WriteBytesWithoutReplacement(const Path& path, const std::string& serialized, FileIdentity& identity) {

		return WriteBytesImpl(path, serialized, false, &identity);
	}

	std::string ReadVerifiedBytes(const Path& path, const std::string& revision) {

		std::ifstream source(Algorithm::ToFileSystemPath(path), std::ios::binary);
		if (!source) {
			throw std::runtime_error("保存データを読み込めません");
		}
		std::string result{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
		const auto bytes = std::span(reinterpret_cast<const uint8_t*>(result.data()), result.size());
		// 実際に読んだbyteへ照合する
		if (source.bad() || ContentHash::SHA256(bytes) != revision) {
			throw std::runtime_error("保存データの内容が変わりました");
		}
		return result;
	}

	bool CopyFileWithoutReplacement(const Path& source, const Path& target, FileIdentity& identity, std::string& revision) {

		// コピー中の元ファイルの書換えと差替えを防ぐ
		const auto nativeSource = Algorithm::ToFileSystemPath(source);
		const HANDLE rawHandle = CreateFileW(
			nativeSource.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (rawHandle == INVALID_HANDLE_VALUE) {
			return false;
		}
		const FileHandle sourceFile(rawHandle, CloseFile);
		std::string copiedRevision;
		const bool copied = WriteTemporary(
			target,
			[&](HANDLE destination) {
				// モデル全体をメモリへ展開せずコピーする
				copiedRevision = ContentHash::ReadSHA256([&](std::span<uint8_t> buffer, size_t& count) {
					DWORD read = 0;
					if (!ReadFile(sourceFile.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
						return false;
					}
					count = read;
					return WriteBuffer(destination, buffer.first(count));
				});
				return !copiedRevision.empty();
			},
			false, &identity);
		if (copied) {
			revision.swap(copiedRevision);
		}
		return copied;
	}

	bool CopyFileAtomically(const Path& source, const Path& target) {

		// 元の保存byteを変換せず復旧する
		std::ifstream file(Algorithm::ToFileSystemPath(source), std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (!file.is_open() || file.bad()) {
			return false;
		}
		file.close();
		return WriteBytes(target, bytes);
	}
}
