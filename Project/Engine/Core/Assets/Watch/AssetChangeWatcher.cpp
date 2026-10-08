#include "AssetChangeWatcher.h"

//============================================================================
//	include
//============================================================================
// windows
#include <windows.h>
// c++
#include <cstdint>
#include <array>
#include <cstddef>
#include <exception>

//============================================================================
//	AssetChangeWatcher classMethods
//============================================================================
Engine::AssetChangeWatcher::~AssetChangeWatcher() {
	Stop();
}

bool Engine::AssetChangeWatcher::Start(const std::filesystem::path& directory) {

	// 監視中なら一度止めてから張り直す
	Stop();
	directory_ = directory;

	std::error_code existsError{};
	if (!std::filesystem::exists(directory, existsError) || existsError) {
		return false;
	}

	// 変更通知を非同期に受け取るディレクトリを開く
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
	// 開始成功を返す前にI/O完了イベントを確保する
	changeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!changeEvent_) {
		Stop();
		return false;
	}
	running_.store(true);
	try {
		thread_ = std::thread(&AssetChangeWatcher::ThreadMain, this);
	} catch (const std::exception&) {

		// スレッド生成に失敗したハンドルを戻す
		Stop();
		return false;
	}
	return true;
}

void Engine::AssetChangeWatcher::Stop() {

	running_.store(false);

	// 待機を解除して通知I/Oを取り消す
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
	if (changeEvent_) {
		CloseHandle(static_cast<HANDLE>(changeEvent_));
		changeEvent_ = nullptr;
	}

	std::scoped_lock lock(mutex_);
	changedPaths_.clear();
	rescanRequired_.store(false);
}

void Engine::AssetChangeWatcher::DrainChanges(std::vector<std::filesystem::path>& outPaths) {

	std::scoped_lock lock(mutex_);
	if (rescanRequired_.exchange(false)) {

		try {
			outPaths.emplace_back(directory_);
		} catch (...) {
			rescanRequired_.store(true);
			throw;
		}
		changedPaths_.clear();
		return;
	}
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

	// I/O完了まで通知バッファを保持する
	alignas(DWORD) std::array<uint8_t, 64 * 1024> buffer{};
	OVERLAPPED overlapped{};
	overlapped.hEvent = static_cast<HANDLE>(changeEvent_);

	// ファイル名と内容の変更を監視する
	const DWORD notifyFilter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
		FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE;

	try {
		while (running_.load()) {

			ResetEvent(overlapped.hEvent);
			DWORD bytesReturned = 0;
			const BOOL ok = ReadDirectoryChangesW(directory, buffer.data(),
				static_cast<DWORD>(buffer.size()), TRUE, notifyFilter, &bytesReturned, &overlapped, nullptr);
			if (!ok) {
				rescanRequired_.store(true);
				break;
			}

			// 変更イベントか停止イベントのどちらかを待つ
			const HANDLE waits[2] = { overlapped.hEvent, stop };
			const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
			if (wait != WAIT_OBJECT_0) {

				// キャンセル完了後に通知バッファを解放する
				CancelIoEx(directory, &overlapped);
				GetOverlappedResult(directory, &overlapped, &bytesReturned, TRUE);
				break;
			}

			DWORD transferred = 0;
			if (!GetOverlappedResult(directory, &overlapped, &transferred, TRUE) || transferred == 0) {

				// 通知欠落は監視ルートの再走査へ戻す
				rescanRequired_.store(true);
				continue;
			}

			// 通知の連鎖から変更ファイルの絶対パスを集める
			std::vector<std::filesystem::path> collected;
			DWORD offset = 0;
			for (;;) {

				// 不完全な通知は個別解析せず再走査する
				constexpr size_t headerSize = offsetof(FILE_NOTIFY_INFORMATION, FileName);
				if (transferred > buffer.size() || offset > transferred || transferred - offset < headerSize) {
					rescanRequired_.store(true);
					break;
				}
				const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
				if (info->FileNameLength % sizeof(WCHAR) != 0 || info->FileNameLength > transferred - offset - headerSize) {
					rescanRequired_.store(true);
					break;
				}
				const size_t nameLength = info->FileNameLength / sizeof(WCHAR);
				if (nameLength != 0) {

					const std::wstring relativeName(info->FileName, nameLength);
					collected.emplace_back(directory_ / relativeName);
				}
				if (info->NextEntryOffset == 0) {
					break;
				}
				if (info->NextEntryOffset < headerSize + info->FileNameLength ||
					info->NextEntryOffset % alignof(DWORD) != 0 || info->NextEntryOffset > transferred - offset) {
					rescanRequired_.store(true);
					break;
				}
				offset += info->NextEntryOffset;
			}

			if (!collected.empty()) {

				std::scoped_lock lock(mutex_);
				// 大量更新は通知をまとめてメモリの増加を抑える
				if (changedPaths_.size() + collected.size() > 4096 || rescanRequired_.load()) {
					changedPaths_.clear();
					rescanRequired_.store(true);
					continue;
				}
				for (std::filesystem::path& path : collected) {
					changedPaths_.emplace_back(std::move(path));
				}
			}
		}
	} catch (const std::exception&) {

		// パス収集に失敗した場合も次回の再走査へ引き継ぐ
		rescanRequired_.store(true);
	}

	running_.store(false);
}
