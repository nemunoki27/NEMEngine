#include "WindowCursor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/Win32Window.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

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

void WindowCursor::ForceShowCursor(bool show) {

	// ShowCursorは内部カウンタ方式なので、目標状態まで回す
	if (show) {

		while (ShowCursor(TRUE) < 0) {}
	} else {

		while (ShowCursor(FALSE) >= 0) {}
	}

}

void WindowCursor::ApplyCursorVisibilityIfNeeded() {

	if (!WinApp::GetHwnd()) {
		return;
	}
	if (GetForegroundWindow() != WinApp::GetHwnd()) {

		ForceShowCursor(true);
		return;
	}
	ForceShowCursor(cursorVisible_);
}

void WindowCursor::SetCursorVisible(bool visible) {

	cursorVisible_ = visible;
	ApplyCursorVisibilityIfNeeded();

	// ついでにクライアント上のカーソル形状も消す
	if (!cursorVisible_) {

		::SetCursor(nullptr);
	}
}

void WindowCursor::ApplyCursorClipIfNeeded() {

	if (!cursorClipEnabled_ || WinApp::GetHwnd() == nullptr) {
		ClipCursor(nullptr);
		return;
	}

	if (!cursorClipEnabled_) {
		ClipCursor(nullptr);
		return;
	}

	// 非アクティブなら解除
	if (GetForegroundWindow() != WinApp::GetHwnd()) {
		ClipCursor(nullptr);
		return;
	}

	RECT client{};
	if (useCustomClipRect_) {

		client = customClientClipRect_;
	} else {

		GetClientRect(WinApp::GetHwnd(), &client);
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
	GetClientRect(WinApp::GetHwnd(), &full);
	client.left = (std::max)(client.left, full.left);
	client.top = (std::max)(client.top, full.top);
	client.right = (std::min)(client.right, full.right);
	client.bottom = (std::min)(client.bottom, full.bottom);
	RECT screenRect = ClientRectToScreenRect(WinApp::GetHwnd(), client);
	ClipCursor(&screenRect);
}

RECT WindowCursor::ClientRectToScreenRect(HWND hwnd, const RECT& clientRect) {

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

void WindowCursor::SetCursorClipEnabled(bool enabled) {

	cursorClipEnabled_ = enabled;
	if (!enabled) {
		useCustomClipRect_ = false;
	}
	ApplyCursorClipIfNeeded();
}

void WindowCursor::ClipCursorToClient() {

	useCustomClipRect_ = false;
	cursorClipEnabled_ = true;
	ApplyCursorClipIfNeeded();
}

void WindowCursor::ClipCursorToClientRect(const Vector2& size, const Vector2& pos) {

	// Rectを設定
	SetCursorClipRect(size, pos);
	useCustomClipRect_ = true;
	cursorClipEnabled_ = true;
	ApplyCursorClipIfNeeded();
}

void WindowCursor::ReleaseCursorClip() {

	cursorClipEnabled_ = false;
	useCustomClipRect_ = false;
	ClipCursor(nullptr);
}

void WindowCursor::SetCursorClipRect(const Vector2& size, const Vector2& pos) {

	// Rectを作成
	RECT clientRect = MakeClientRect(size, pos);
	customClientClipRect_ = clientRect;
}
