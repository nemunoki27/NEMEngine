#include "Win32Window.h"

//============================================================================
//	include
//============================================================================
#include "WindowFileDrop.h"
#include "WindowInputBridge.h"

// c++
#include <algorithm>

// windows
#include <mmsystem.h>
#include <shellapi.h>

using namespace Engine;

#pragma comment(lib,"winmm.lib")

// 外部ファイルドロップ用
#pragma comment(lib,"shell32.lib")

//============================================================================
//	WinApp classMethods
//============================================================================

HWND WinApp::hwnd_ = nullptr;
WindowCursor WinApp::cursor_;
WindowPlacement WinApp::placement_;
bool (*WinApp::closeRequestCallback_)() = nullptr;
WinApp::MessageHandler WinApp::messageHandler_ = nullptr;

void WinApp::Create(uint32_t sizeX, uint32_t sizeY, const wchar_t* title) {

	timeBeginPeriod(1);
	EnablePerMonitorDpiAwareness();
	RegisterWindowClass();
	windowStyle_ = WS_OVERLAPPEDWINDOW;
	const RECT windowRect = placement_.Prepare(sizeX, sizeY, windowStyle_);

	hwnd_ = CreateWindow(
		L"WindowClass",
		title,
		windowStyle_,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		GetModuleHandle(nullptr),
		nullptr);

	// 外部エクスプローラーからのファイルドロップを受け付ける
	DragAcceptFiles(hwnd_, TRUE);

	ShowWindow(hwnd_, SW_MAXIMIZE);

	cursor_.ApplyCursorVisibilityIfNeeded();
	cursor_.ApplyCursorClipIfNeeded();
}

bool WinApp::HandleExternalFileDrop(HWND hwnd, WPARAM wparam) {

	WindowFileDrop drop = WindowFileDrop::Read(hwnd, wparam);
	WindowInputBridge::PushDrop(drop);
	return !drop.paths.empty();
}

void WinApp::EnablePerMonitorDpiAwareness() {

	using SetProcessDpiAwarenessContextFunction = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	if (!user32) {
		return;
	}

	auto setProcessDpiAwarenessContext =
		reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
			GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
	if (setProcessDpiAwarenessContext) {
		setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	}
}

bool WinApp::ProcessMessage() {

	MSG msg{};
	bool quitRequested = false;

	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			quitRequested = true;
			continue;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return quitRequested;
}

Vector2I WinApp::GetClientSize() {

	if (!hwnd_ || !IsWindow(hwnd_) || IsIconic(hwnd_)) {
		return {};
	}

	RECT client{};
	if (!GetClientRect(hwnd_, &client)) {
		return {};
	}
	return {
		static_cast<int32_t>((std::max)(client.right - client.left, 0L)),
		static_cast<int32_t>((std::max)(client.bottom - client.top, 0L))
	};
}

void WinApp::RequestCloseWindow() {

	if (!hwnd_) {
		return;
	}
	PostMessage(hwnd_, WM_CLOSE, 0, 0);
}

void WinApp::SetFullscreen(bool fullscreen) {

	if (!hwnd_ || placement_.IsFullscreen() == fullscreen) {
		return;
	}
	placement_.SetFullscreen(hwnd_, fullscreen);
	cursor_.ApplyCursorVisibilityIfNeeded();
	cursor_.ApplyCursorClipIfNeeded();
}

LRESULT WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

	// WM_SETCURSOR：非表示時はクライアント内だけNULLカーソルにする
	if (msg == WM_SETCURSOR) {
		if (!cursor_.IsCursorVisible() && LOWORD(lparam) == HTCLIENT) {
			::SetCursor(nullptr);
			return TRUE;
		}
	}

	// フォーカス/アクティブ変化：クリップ解除・カーソル表示の安全復帰
	switch (msg) {
	case WM_ACTIVATE:
		if (LOWORD(wparam) == WA_INACTIVE) {

			// 非アクティブ：必ず解除＆表示
			ClipCursor(nullptr);
			cursor_.ForceShowCursor(true);
			WindowInputBridge::NotifyFocus(false);
		} else {

			cursor_.ApplyCursorVisibilityIfNeeded();
			cursor_.ApplyCursorClipIfNeeded();
			WindowInputBridge::NotifyFocus(true);
		}
		return 0;
	case WM_SETFOCUS:

		cursor_.ApplyCursorVisibilityIfNeeded();
		cursor_.ApplyCursorClipIfNeeded();
		WindowInputBridge::NotifyFocus(true);
		return 0;

	case WM_KILLFOCUS:

		ClipCursor(nullptr);
		cursor_.ForceShowCursor(true);
		WindowInputBridge::NotifyFocus(false);
		return 0;

	case WM_CHAR:

		// gameplay向け文字入力で制御文字以外をframe-localテキストへ溜める、ImGuiとは独立
		WindowInputBridge::AppendCharacter(wparam);
		break;
	case WM_SIZE:
	case WM_MOVE:

		// ウィンドウ移動・サイズ変更後は再適用
		cursor_.ApplyCursorClipIfNeeded();
		return 0;

	case WM_DROPFILES:
	{
		HandleExternalFileDrop(hwnd, wparam);
		return 0;
	}
	}

	if (messageHandler_ && messageHandler_(hwnd, msg, wparam, lparam)) {

		return true;
	}

	switch (msg) {
	case WM_CLOSE:

		// 終了前にエディタ側へ未保存確認を通す
		if (closeRequestCallback_ && !closeRequestCallback_()) {
			return 0;
		}
		DestroyWindow(hwnd);
		return 0;

	case WM_DESTROY:

		// 念のため安全復帰
		ClipCursor(nullptr);
		cursor_.ForceShowCursor(true);
		if (hwnd == hwnd_) {
			hwnd_ = nullptr;
		}
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void WinApp::RegisterWindowClass() {

	// Window Procedure
	WNDCLASS wc{};
	// ウィンドウプロシージャ(Window Procedure)
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名
	wc.lpszClassName = L"WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);
}

void WinApp::SetCursorVisible(bool visible) {

	cursor_.SetCursorVisible(visible);
}

void WinApp::SetCursorClipEnabled(bool enabled) {

	cursor_.SetCursorClipEnabled(enabled);
}

void WinApp::ClipCursorToClient() {

	cursor_.ClipCursorToClient();
}

void WinApp::ClipCursorToClientRect(const Vector2& size, const Vector2& pos) {

	cursor_.ClipCursorToClientRect(size, pos);
}

void WinApp::ReleaseCursorClip() {

	cursor_.ReleaseCursorClip();
}

void WinApp::SetCursorClipRect(const Vector2& size, const Vector2& pos) {

	cursor_.SetCursorClipRect(size, pos);
}
