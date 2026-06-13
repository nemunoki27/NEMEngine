#include "ManagedSourceWatcher.h"

//============================================================================
//	include
//============================================================================
// windows
#include <windows.h>
// c++
#include <cstdint>
#include <vector>

//============================================================================
//	ManagedSourceWatcher classMethods
//============================================================================
Engine::ManagedSourceWatcher::~ManagedSourceWatcher() {
	Stop();
}

bool Engine::ManagedSourceWatcher::Start(const std::filesystem::path& directory) {

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

	directoryHandle_ = handle;
	stopEvent_ = stop;
	changed_.store(false);
	running_.store(true);
	thread_ = std::thread(&ManagedSourceWatcher::ThreadMain, this);
	return true;
}

void Engine::ManagedSourceWatcher::Stop() {

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
}

void Engine::ManagedSourceWatcher::ThreadMain() {

	const HANDLE directory = static_cast<HANDLE>(directoryHandle_);
	const HANDLE stop = static_cast<HANDLE>(stopEvent_);

	OVERLAPPED overlapped{};
	overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!overlapped.hEvent) {
		return;
	}

	// 変更通知バッファ
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
		if (GetOverlappedResult(directory, &overlapped, &transferred, FALSE)) {

			// 細かな種別判定はせず変更ありとして記録し、実際の差分はscan/compareで確定する
			changed_.store(true);
		}
	}

	CloseHandle(overlapped.hEvent);
}
