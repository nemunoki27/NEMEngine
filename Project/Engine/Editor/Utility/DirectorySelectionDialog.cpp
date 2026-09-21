#include "EditorShell.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>

// c++
#include <atomic>
#include <mutex>
#include <system_error>
#include <thread>

// windows
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#pragma comment(lib, "shell32.lib")

namespace {

	// ダイアログスレッドが所有するウィンドウを閉じる
	BOOL CALLBACK CloseDialogWindow(HWND hwnd, LPARAM) {

		::PostMessage(hwnd, WM_CLOSE, 0, 0);
		return TRUE;
	}
}

//============================================================================
//	DirectorySelectionDialog classMethods
//============================================================================
struct Engine::EditorShell::DirectorySelectionDialog::State {

	std::thread thread;
	std::mutex mutex;
	std::optional<std::filesystem::path> selected;
	std::atomic_bool active = false;
	std::atomic_bool completed = false;
	std::atomic_bool cancelRequested = false;
	std::atomic<DWORD> threadID = 0;
};

Engine::EditorShell::DirectorySelectionDialog::DirectorySelectionDialog() :
	state_(std::make_unique<State>()) {
}

Engine::EditorShell::DirectorySelectionDialog::~DirectorySelectionDialog() {

	if (!state_->thread.joinable()) {
		return;
	}
	if (!state_->completed) {

		state_->cancelRequested = true;
		const DWORD threadID = state_->threadID;
		if (threadID != 0) {
			::EnumThreadWindows(threadID, CloseDialogWindow, 0);
			::PostThreadMessage(threadID, WM_QUIT, 0, 0);
		}
	}
	state_->thread.join();
}

bool Engine::EditorShell::DirectorySelectionDialog::Open(
	const std::filesystem::path& initialDirectory) {

	if (state_->active) {
		return false;
	}
	if (state_->thread.joinable()) {
		state_->thread.join();
	}

	{
		std::lock_guard lock(state_->mutex);
		state_->selected.reset();
	}
	state_->active = true;
	state_->completed = false;
	state_->cancelRequested = false;

	State* state = state_.get();
	const HWND owner = WinApp::GetHwnd();
	state_->thread = std::thread([state, owner, initialDirectory]() {

		state->threadID = ::GetCurrentThreadId();
		const HRESULT initializeResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (SUCCEEDED(initializeResult)) {

			IFileOpenDialog* dialog = nullptr;
			const HRESULT createResult = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
				CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
			if (SUCCEEDED(createResult) && dialog) {

				FILEOPENDIALOGOPTIONS options{};
				if (SUCCEEDED(dialog->GetOptions(&options))) {
					dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
				}
				dialog->SetTitle(L"出力先を選択");

				std::error_code ec;
				if (!initialDirectory.empty() && std::filesystem::is_directory(initialDirectory, ec)) {

					IShellItem* initialItem = nullptr;
					if (SUCCEEDED(::SHCreateItemFromParsingName(initialDirectory.wstring().c_str(),
						nullptr, IID_PPV_ARGS(&initialItem))) && initialItem) {

						dialog->SetFolder(initialItem);
						initialItem->Release();
					}
				}

				if (!state->cancelRequested && SUCCEEDED(dialog->Show(owner))) {

					IShellItem* item = nullptr;
					if (SUCCEEDED(dialog->GetResult(&item)) && item) {

						PWSTR path = nullptr;
						if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {

							std::lock_guard lock(state->mutex);
							state->selected = std::filesystem::path(path);
							::CoTaskMemFree(path);
						}
						item->Release();
					}
				}
				dialog->Release();
			}
			::CoUninitialize();
		}
		state->threadID = 0;
		state->completed = true;
		});
	return true;
}

bool Engine::EditorShell::DirectorySelectionDialog::Poll(
	std::optional<std::filesystem::path>& outSelected) {

	outSelected.reset();
	if (!state_->active || !state_->completed) {
		return false;
	}
	if (state_->thread.joinable()) {
		state_->thread.join();
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

