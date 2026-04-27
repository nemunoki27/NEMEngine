#include "WinApp.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Math.h>

// imgui
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#pragma comment(lib,"winmm.lib")

//============================================================================
//	WinApp classMethods
//============================================================================

namespace {

	// クライアント座標系の矩形を作成する
	RECT MakeClientRect(const Vector2& size, const Vector2& pos) {

		const float halfX = size.x * 0.5f;
		const float halfY = size.y * 0.5f;

		const LONG left = static_cast<LONG>(std::floor(pos.x - halfX));
		const LONG top = static_cast<LONG>(std::floor(pos.y - halfY));
		const LONG right = static_cast<LONG>(std::ceil(pos.x + halfX));
		const LONG bottom = static_cast<LONG>(std::ceil(pos.y + halfY));

		RECT rect{ left, top, right, bottom };
		return rect;
	}
}

HWND WinApp::hwnd_ = nullptr;
bool WinApp::cursorClipEnabled_ = false;
bool WinApp::useCustomClipRect_ = false;
bool WinApp::cursorVisible_ = true;
RECT WinApp::customClientClipRect_{};
RECT WinApp::windowRect_{};

void WinApp::ForceShowCursor(bool show) {

	// ShowCursor は内部カウンタ方式なので、目標状態まで回す
	if (show) {

		while (ShowCursor(TRUE) < 0) {}
	} else {

		while (ShowCursor(FALSE) >= 0) {}
	}

}

void WinApp::ApplyCursorVisibilityIfNeeded() {

	if (!hwnd_) {
		return;
	}
	if (GetForegroundWindow() != hwnd_) {

		ForceShowCursor(true);
		return;
	}
	ForceShowCursor(cursorVisible_);
}

void WinApp::SetCursorVisible(bool visible) {

	cursorVisible_ = visible;
	ApplyCursorVisibilityIfNeeded();

	// ついでにクライアント上のカーソル形状も消す
	if (!cursorVisible_) {

		::SetCursor(nullptr);
	}
}

void WinApp::ApplyCursorClipIfNeeded() {

	if (!cursorClipEnabled_ || hwnd_ == nullptr) {
		ClipCursor(nullptr);
		return;
	}

	if (!cursorClipEnabled_) {
		ClipCursor(nullptr);
		return;
	}

	// 非アクティブなら解除
	if (GetForegroundWindow() != hwnd_) {
		ClipCursor(nullptr);
		return;
	}

	RECT client{};
	if (useCustomClipRect_) {

		client = customClientClipRect_;
	} else {

		GetClientRect(hwnd_, &client);
	}

	// 念のため正規化
	if (client.left > client.right) {
		std::swap(client.left, client.right);
	}
	if (client.top > client.bottom) {
		std::swap(client.top, client.bottom);
	}

	// クライアント範囲にクランプ
	RECT full{};
	GetClientRect(hwnd_, &full);
	client.left = (std::max)(client.left, full.left);
	client.top = (std::max)(client.top, full.top);
	client.right = (std::min)(client.right, full.right);
	client.bottom = (std::min)(client.bottom, full.bottom);
	RECT screenRect = ClientRectToScreenRect(hwnd_, client);
	ClipCursor(&screenRect);
}

RECT WinApp::ClientRectToScreenRect(HWND hwnd, const RECT& clientRect) {

	POINT lt{ clientRect.left,  clientRect.top };
	POINT rb{ clientRect.right, clientRect.bottom };
	ClientToScreen(hwnd, &lt);
	ClientToScreen(hwnd, &rb);

	RECT screenRect{};
	screenRect.left = lt.x;
	screenRect.top = lt.y;
	screenRect.right = rb.x;
	screenRect.bottom = rb.y;
	return screenRect;
}

void WinApp::Create(uint32_t sizeX, uint32_t sizeY, const wchar_t* title) {

	timeBeginPeriod(1);
	RegisterWindowClass();

	windowRect_.right = sizeX;
	windowRect_.bottom = sizeY;

	windowStyle_ = WS_OVERLAPPEDWINDOW;

	AdjustWindowRect(&windowRect_, windowStyle_, false);

	hwnd_ = CreateWindow(
		L"WindowClass",
		title,
		windowStyle_,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect_.right - windowRect_.left,
		windowRect_.bottom - windowRect_.top,
		nullptr,
		nullptr,
		GetModuleHandle(nullptr),
		nullptr);

	ShowWindow(hwnd_, SW_MAXIMIZE);

	ApplyCursorVisibilityIfNeeded();
	ApplyCursorClipIfNeeded();
}

bool WinApp::ProcessMessage() {

	MSG msg{};

	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if (msg.message == WM_QUIT) {

		return true;
	}

	return false;
}

void WinApp::SetFullscreen(bool fullscreen) {

	if (fullscreen) {

		// 現在のウィンドウ情報を保存（復元用）
		GetWindowRect(hwnd_, &windowRect_);

		// ウィンドウがあるモニターの領域を取得
		HMONITOR hMon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{};
		mi.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(hMon, &mi);

		// 境界線なしスタイルに変更
		SetWindowLong(hwnd_, GWL_STYLE, WS_POPUP);
		SetWindowLong(hwnd_, GWL_EXSTYLE, 0);

		// モニターサイズに合わせて最大化
		SetWindowPos(
			hwnd_,
			HWND_TOP,
			mi.rcMonitor.left,
			mi.rcMonitor.top,
			mi.rcMonitor.right - mi.rcMonitor.left,
			mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_SHOWWINDOW
		);

		ShowWindow(hwnd_, SW_SHOWNORMAL);
		SetForegroundWindow(hwnd_);
		SetFocus(hwnd_);
	} else {

		// 通常のウィンドウスタイルに戻す
		SetWindowLong(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW);
		SetWindowLong(hwnd_, GWL_EXSTYLE, 0);

		// 保存していたウィンドウの位置とサイズに復元
		SetWindowPos(
			hwnd_,
			HWND_NOTOPMOST,
			windowRect_.left,
			windowRect_.top,
			windowRect_.right - windowRect_.left,
			windowRect_.bottom - windowRect_.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_SHOWWINDOW);

		ShowWindow(hwnd_, SW_SHOWNORMAL);
		SetForegroundWindow(hwnd_);
		SetFocus(hwnd_);
	}
	ApplyCursorVisibilityIfNeeded();
	ApplyCursorClipIfNeeded();
}

void WinApp::SetCursorClipEnabled(bool enabled) {

	cursorClipEnabled_ = enabled;
	if (!enabled) {
		useCustomClipRect_ = false;
	}
	ApplyCursorClipIfNeeded();
}

void WinApp::ClipCursorToClient() {

	useCustomClipRect_ = false;
	cursorClipEnabled_ = true;
	ApplyCursorClipIfNeeded();
}

void WinApp::ClipCursorToClientRect(const Vector2& size, const Vector2& pos) {

	// Rectを設定
	SetCursorClipRect(size, pos);
	useCustomClipRect_ = true;
	cursorClipEnabled_ = true;
	ApplyCursorClipIfNeeded();
}

void WinApp::ReleaseCursorClip() {

	cursorClipEnabled_ = false;
	useCustomClipRect_ = false;
	ClipCursor(nullptr);
}

void Engine::WinApp::SetCursorClipRect(const Vector2& size, const Vector2& pos) {

	// Rectを作成
	RECT clientRect = MakeClientRect(size, pos);
	customClientClipRect_ = clientRect;
}

LRESULT WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

	// WM_SETCURSOR：非表示時はクライアント内だけNULLカーソルにする
	if (msg == WM_SETCURSOR) {
		if (!cursorVisible_ && LOWORD(lparam) == HTCLIENT) {
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
			ForceShowCursor(true);
		} else {

			ApplyCursorVisibilityIfNeeded();
			ApplyCursorClipIfNeeded();
		}
		return 0;
	case WM_SETFOCUS:

		ApplyCursorVisibilityIfNeeded();
		ApplyCursorClipIfNeeded();
		return 0;

	case WM_KILLFOCUS:

		ClipCursor(nullptr);
		ForceShowCursor(true);
		return 0;
	case WM_SIZE:
	case WM_MOVE:

		// ウィンドウ移動・サイズ変更後は再適用
		ApplyCursorClipIfNeeded();
		return 0;
	}

	// ImGuiマウス有効
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {

		return true;
	}

	switch (msg) {
	case WM_DESTROY:

		// 念のため安全復帰
		ClipCursor(nullptr);
		ForceShowCursor(true);
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