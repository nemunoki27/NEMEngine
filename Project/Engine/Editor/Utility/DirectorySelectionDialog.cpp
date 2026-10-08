#include "EditorShell.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>

// c++
#include <atomic>
#include <cstdint>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>

// windows
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#pragma comment(lib, "shell32.lib")

namespace {

	// 表示中のダイアログと終了要求を同じスレッドで参照する
	struct DialogCancellation {

		IFileDialog& dialog;			   // 表示中のダイアログ
		const std::atomic_bool& requested; // 所有元の終了要求
	};

	// タイマーの呼出し中だけ表示中のダイアログを参照する
	thread_local DialogCancellation* currentCancellation = nullptr;

	// 表示開始後に届いた終了要求を処理する
	void CALLBACK CancelDialog(HWND, UINT, UINT_PTR, DWORD) {

		if (currentCancellation && currentCancellation->requested.load()) {
			currentCancellation->dialog.Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
		}
	}

	// 所有ウィンドウへの同期通知を処理しながら終了を待つ
	void JoinDialogThread(std::thread& thread) {

		if (!thread.joinable()) {
			return;
		}
		const HANDLE handle = thread.native_handle();
		while (::MsgWaitForMultipleObjectsEx(1, &handle, INFINITE, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0 + 1) {

			// 終了中は入力や終了メッセージを取り出さない
			MSG message{};
			::PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
		}
		thread.join();
	}

	// COM資源を保持してフォルダー選択を実行する
	std::optional<std::filesystem::path> SelectDirectory(
		const std::filesystem::path& initialDirectory, HWND owner, const std::atomic_bool& cancelRequested, HRESULT& result) {

		result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(result)) {
			return {};
		}
		Engine::ScopedCleanup uninitialize([]() noexcept { ::CoUninitialize(); });
		Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
		result = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
		if (FAILED(result)) {
			return {};
		}

		// フォルダー選択だけを許可する
		FILEOPENDIALOGOPTIONS options{};
		result = dialog->GetOptions(&options);
		if (SUCCEEDED(result)) {
			result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
		}
		if (FAILED(result)) {
			return {};
		}
		dialog->SetTitle(L"出力先を選択");
		std::error_code ec;
		if (!initialDirectory.empty() && std::filesystem::is_directory(initialDirectory, ec)) {

			Microsoft::WRL::ComPtr<IShellItem> initialItem;
			if (SUCCEEDED(
					::SHCreateItemFromParsingName(initialDirectory.wstring().c_str(), nullptr, IID_PPV_ARGS(&initialItem)))) {
				dialog->SetFolder(initialItem.Get());
			}
		}

		// 表示直前の終了要求もタイマーで受け取る
		DialogCancellation cancellation{*dialog.Get(), cancelRequested};
		Engine::ScopedValue activeCancellation(currentCancellation, &cancellation);
		const UINT_PTR timer = ::SetTimer(nullptr, 0, 30, CancelDialog);
		if (timer == 0) {
			result = E_FAIL;
			return {};
		}
		Engine::ScopedCleanup stopTimer([timer]() noexcept { ::KillTimer(nullptr, timer); });
		if (cancelRequested.load()) {
			result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
			return {};
		}
		result = dialog->Show(owner);
		if (FAILED(result) || cancelRequested.load()) {
			return {};
		}

		// 選択結果をコピーしてCOM側の領域を解放する
		Microsoft::WRL::ComPtr<IShellItem> item;
		result = dialog->GetResult(&item);
		if (FAILED(result)) {
			return {};
		}
		PWSTR path = nullptr;
		result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
		Engine::ScopedCleanup freePath([&path]() noexcept { ::CoTaskMemFree(path); });
		if (FAILED(result) || !path) {
			return {};
		}
		return std::filesystem::path(path);
	}
}

//============================================================================
//	DirectorySelectionDialog classMethods
//============================================================================
// ダイアログの実行スレッドと選択結果を所有する
struct Engine::EditorShell::DirectorySelectionDialog::State {

	std::thread thread;							   // ダイアログの実行スレッド
	std::mutex mutex;							   // 選択結果の排他
	std::optional<std::filesystem::path> selected; // 選択したフォルダー
	std::atomic_bool active = false;			   // 結果取得前の状態
	std::atomic_bool completed = false;			   // スレッド処理の完了
	std::atomic_bool cancelRequested = false;	   // 所有元の終了要求
	HRESULT result = S_OK;						   // 完了通知前に確定する実行結果
};

Engine::EditorShell::DirectorySelectionDialog::DirectorySelectionDialog() : state_(std::make_unique<State>()) {
}

Engine::EditorShell::DirectorySelectionDialog::~DirectorySelectionDialog() {

	// 表示中のダイアログを閉じてから所有状態を破棄する
	state_->cancelRequested = true;
	JoinDialogThread(state_->thread);
}

bool Engine::EditorShell::DirectorySelectionDialog::Open(const std::filesystem::path& initialDirectory) {

	if (state_->active) {
		return false;
	}
	JoinDialogThread(state_->thread);

	{
		std::lock_guard lock(state_->mutex);
		state_->selected.reset();
	}
	state_->active = true;
	state_->completed = false;
	state_->cancelRequested = false;
	state_->result = S_OK;

	State* state = state_.get();
	const HWND owner = WinApp::GetHwnd();
	try {

		state_->thread = std::thread([state, owner, initialDirectory]() {
			// 失敗した場合も完了を通知する
			ScopedCleanup complete([state]() noexcept { state->completed = true; });
			try {

				auto selected = SelectDirectory(initialDirectory, owner, state->cancelRequested, state->result);
				std::lock_guard lock(state->mutex);
				state->selected = std::move(selected);
			} catch (...) {
				state->result = E_FAIL;
			}
		});
	} catch (const std::exception& exception) {

		// スレッドを開始できなければ再度開ける状態へ戻す
		state_->active = false;
		Logger::Output(
			LogType::Engine, spdlog::level::warn, "[EditorShell] フォルダー選択を開始できません: {}", exception.what());
		return false;
	}
	return true;
}

bool Engine::EditorShell::DirectorySelectionDialog::Poll(std::optional<std::filesystem::path>& outSelected) {

	outSelected.reset();
	if (!state_->active || !state_->completed) {
		return false;
	}
	JoinDialogThread(state_->thread);
	if (FAILED(state_->result) && state_->result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "[EditorShell] フォルダー選択に失敗しました: 0x{:08X}",
			static_cast<uint32_t>(state_->result));
	}

	{
		std::lock_guard lock(state_->mutex);
		outSelected = std::move(state_->selected);
		state_->selected.reset();
	}
	state_->active = false;
	state_->completed = false;
	return true;
}

bool Engine::EditorShell::DirectorySelectionDialog::IsOpen() const {

	return state_->active;
}
