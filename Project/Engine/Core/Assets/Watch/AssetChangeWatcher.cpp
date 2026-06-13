#include "AssetChangeWatcher.h"

//============================================================================
//	include
//============================================================================
// windows
#include <windows.h>
// c++
#include <cstdint>

//============================================================================
//	AssetChangeWatcher classMethods
//============================================================================
Engine::AssetChangeWatcher::~AssetChangeWatcher() {
	Stop();
}

bool Engine::AssetChangeWatcher::Start(const std::filesystem::path& directory) {

	// 監視中なら一度止めてから張り直す
	Stop();

	std::error_code existsError{};
	if (!std::filesystem::exists(directory, existsError) || existsError) {
		return false;
	}

	// 変更通知を受け取るためディレクトリをFILE_LIST_DIRECTORYかつoverlappedで開く
	const HANDLE handle = CreateFileW(directory.wstring().c_str(), FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		return false;
	}

	const HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!stop) {
		CloseHandle(handle);
		return false;
	}

	directory_ = directory;
	directoryHandle_ = handle;
	stopEvent_ = stop;
	running_.store(true);
	thread_ = std::thread(&AssetChangeWatcher::ThreadMain, this);
	return true;
}

void Engine::AssetChangeWatcher::Stop() {

	running_.store(false);

	// 待機中のスレッドを起こし、進行中のReadDirectoryChangesWを解除する
	if (stopEvent_) {
		SetEvent(static_cast<HANDLE>(stopEvent_));
	}
	if (directoryHandle_) {
		CancelIoEx(static_cast<HANDLE>(directoryHandle_), nullptr);
	}
	if (thread_.joinable()) {
		thread_.join();
	}

	// ハンドルを後始末する
	if (directoryHandle_) {
		CloseHandle(static_cast<HANDLE>(directoryHandle_));
		directoryHandle_ = nullptr;
	}
	if (stopEvent_) {
		CloseHandle(static_cast<HANDLE>(stopEvent_));
		stopEvent_ = nullptr;
	}

	std::scoped_lock lock(mutex_);
	changedPaths_.clear();
}

void Engine::AssetChangeWatcher::DrainChanges(std::vector<std::filesystem::path>& outPaths) {

	std::scoped_lock lock(mutex_);
	if (changedPaths_.empty()) {
		return;
	}
	for (std::filesystem::path& path : changedPaths_) {
		outPaths.emplace_back(std::move(path));
	}
	changedPaths_.clear();
}

void Engine::AssetChangeWatcher::ThreadMain() {

	const HANDLE directory = static_cast<HANDLE>(directoryHandle_);
	const HANDLE stop = static_cast<HANDLE>(stopEvent_);

	OVERLAPPED overlapped{};
	overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!overlapped.hEvent) {
		return;
	}

	// 変更通知バッファ、深い階層でも溢れにくいよう大きめに確保する
	std::vector<uint8_t> buffer(64 * 1024);
	const DWORD notifyFilter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
		FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE;

	while (running_.load()) {

		ResetEvent(overlapped.hEvent);
		DWORD bytesReturned = 0;
		const BOOL ok = ReadDirectoryChangesW(directory, buffer.data(),
			static_cast<DWORD>(buffer.size()), TRUE, notifyFilter, &bytesReturned, &overlapped, nullptr);
		if (!ok) {
			break;
		}

		// 変更イベントか停止イベントのどちらかを待つ
		const HANDLE waits[2] = { overlapped.hEvent, stop };
		const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
		if (wait != WAIT_OBJECT_0) {

			// 停止要求やエラーは進行中のI/Oを解除して抜ける
			CancelIoEx(directory, nullptr);
			break;
		}

		DWORD transferred = 0;
		if (!GetOverlappedResult(directory, &overlapped, &transferred, FALSE) || transferred == 0) {
			continue;
		}

		// FILE_NOTIFY_INFORMATIONの連鎖を辿って、変更ファイルの絶対パスを集める
		std::vector<std::filesystem::path> collected;
		DWORD offset = 0;
		for (;;) {

			const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
			const size_t nameLength = info->FileNameLength / sizeof(WCHAR);
			if (nameLength != 0) {

				const std::wstring relativeName(info->FileName, nameLength);
				collected.emplace_back(directory_ / relativeName);
			}
			if (info->NextEntryOffset == 0) {
				break;
			}
			offset += info->NextEntryOffset;
		}

		if (!collected.empty()) {

			std::scoped_lock lock(mutex_);
			for (std::filesystem::path& path : collected) {
				changedPaths_.emplace_back(std::move(path));
			}
		}
	}

	CloseHandle(overlapped.hEvent);
}
