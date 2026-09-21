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
