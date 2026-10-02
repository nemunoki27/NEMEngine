#include "WindowPlacement.h"

using namespace Engine;

void WindowPlacement::SetFullscreen(HWND hwnd, bool fullscreen) {

	if (!hwnd || fullscreen_ == fullscreen) {
		return;
	}

	if (fullscreen) {

		// 現在のウィンドウ情報を復元用に保存する
		GetWindowRect(hwnd, &windowRect_);

		// ウィンドウがあるモニターの領域を取得
		HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{};
		mi.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(hMon, &mi);

		// 境界線なしスタイルに変更
		SetWindowLong(hwnd, GWL_STYLE, WS_POPUP);
		SetWindowLong(hwnd, GWL_EXSTYLE, 0);

		// モニターサイズに合わせて最大化
		SetWindowPos(
			hwnd,
			HWND_TOP,
			mi.rcMonitor.left,
			mi.rcMonitor.top,
			mi.rcMonitor.right - mi.rcMonitor.left,
			mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_SHOWWINDOW
		);

		ShowWindow(hwnd, SW_SHOWNORMAL);
		SetForegroundWindow(hwnd);
		SetFocus(hwnd);
	} else {

		// 通常のウィンドウスタイルに戻す
		SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
		SetWindowLong(hwnd, GWL_EXSTYLE, 0);

		// 保存していたウィンドウの位置とサイズに復元
		SetWindowPos(
			hwnd,
			HWND_NOTOPMOST,
			windowRect_.left,
			windowRect_.top,
			windowRect_.right - windowRect_.left,
			windowRect_.bottom - windowRect_.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_SHOWWINDOW);

		ShowWindow(hwnd, SW_SHOWNORMAL);
		SetForegroundWindow(hwnd);
		SetFocus(hwnd);
	}
	fullscreen_ = fullscreen;
}

RECT WindowPlacement::Prepare(uint32_t width, uint32_t height, DWORD style) {

	fullscreen_ = false;
	windowRect_.right = width;
	windowRect_.bottom = height;
	AdjustWindowRect(&windowRect_, style, false);
	return windowRect_;
}

Vector2I WindowPlacement::GetMaximumClientSize(HWND hwnd) const {

	const HMONITOR monitor = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) :
		MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO monitorInfo{};
	monitorInfo.cbSize = sizeof(monitorInfo);
	if (!GetMonitorInfo(monitor, &monitorInfo)) {
		return { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
	}

	RECT frame{};
	const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : USER_DEFAULT_SCREEN_DPI;
	AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
	const int32_t frameWidth = frame.right - frame.left;
	const int32_t frameHeight = frame.bottom - frame.top;
	return {
		static_cast<int32_t>((std::max)(1L,
			monitorInfo.rcWork.right - monitorInfo.rcWork.left - frameWidth)),
		static_cast<int32_t>((std::max)(1L,
			monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - frameHeight))
	};
}

bool WindowPlacement::BeginClientPreview(HWND hwnd, uint32_t width, uint32_t height) {

	if (!hwnd || clientPreview_ || fullscreen_) {
		return false;
	}

	previewPlacement_ = { sizeof(WINDOWPLACEMENT) };
	if (!GetWindowPlacement(hwnd, &previewPlacement_)) {
		return false;
	}
	previewStyle_ = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_STYLE));
	previewExStyle_ = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_EXSTYLE));

	const Vector2I maximum = GetMaximumClientSize(hwnd);
	const LONG clientWidth = (std::clamp)(
		static_cast<LONG>(width), 1L, static_cast<LONG>(maximum.x));
	const LONG clientHeight = (std::clamp)(
		static_cast<LONG>(height), 1L, static_cast<LONG>(maximum.y));
	RECT windowRect{ 0, 0, clientWidth, clientHeight };
	AdjustWindowRectExForDpi(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(hwnd));

	MONITORINFO monitorInfo{};
	monitorInfo.cbSize = sizeof(monitorInfo);
	GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
	const LONG windowWidth = windowRect.right - windowRect.left;
	const LONG windowHeight = windowRect.bottom - windowRect.top;
	const LONG x = monitorInfo.rcWork.left +
		(monitorInfo.rcWork.right - monitorInfo.rcWork.left - windowWidth) / 2;
	const LONG y = monitorInfo.rcWork.top +
		(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - windowHeight) / 2;

	// 最大化を解除して製品画像と同じクライアントサイズへ変更する
	ShowWindow(hwnd, SW_RESTORE);
	SetWindowLongPtr(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	SetWindowPos(hwnd, nullptr, x, y, windowWidth, windowHeight,
		SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
	clientPreview_ = true;
	return true;
}

void WindowPlacement::EndClientPreview(HWND hwnd) {

	if (!hwnd || !clientPreview_) {
		return;
	}

	// 保存したStyleと最大化状態を含む配置へ戻す
	SetWindowLongPtr(hwnd, GWL_STYLE, previewStyle_);
	SetWindowLongPtr(hwnd, GWL_EXSTYLE, previewExStyle_);
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);

	// 通常配置へ戻してから最大化状態を復元する
	const bool maximized = previewPlacement_.showCmd == SW_SHOWMAXIMIZED;
	WINDOWPLACEMENT placement = previewPlacement_;
	placement.showCmd = SW_SHOWNORMAL;
	ShowWindow(hwnd, SW_RESTORE);
	SetWindowPlacement(hwnd, &placement);
	ShowWindow(hwnd, maximized ? SW_MAXIMIZE : SW_SHOWNORMAL);
	clientPreview_ = false;
}
