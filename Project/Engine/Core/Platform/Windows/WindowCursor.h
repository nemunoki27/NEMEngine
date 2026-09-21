#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>

// windows
#include <Windows.h>

namespace Engine {

	//============================================================================
	//	WindowCursor class
	//	メインウィンドウのカーソル表示と範囲を管理する
	//============================================================================
	class WindowCursor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// カーソル範囲制限をON/OFF
		void SetCursorClipEnabled(bool enabled);
		// クライアント座標系の任意矩形でクリップ
		void ClipCursorToClientRect(const Vector2& size, const Vector2& pos);
		// ウィンドウのクライアント領域にクリップ
		void ClipCursorToClient();
		// 解除
		void ReleaseCursorClip();

		// カーソルクリップ範囲のセット
		void SetCursorClipRect(const Vector2& size, const Vector2& pos);

		// カーソルの表示/非表示
		void SetCursorVisible(bool visible);
		bool IsCursorVisible() { return cursorVisible_; }

		// フォーカスに合わせて表示と範囲を反映する
		void ApplyCursorVisibilityIfNeeded();
		void ApplyCursorClipIfNeeded();
		// OSの表示カウンタを指定状態へ揃える
		static void ForceShowCursor(bool show);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		bool cursorClipEnabled_ = false;
		bool useCustomClipRect_ = false;
		bool cursorVisible_ = true;
		RECT customClientClipRect_{};

		// クライアント矩形を画面座標へ変換する
		static RECT ClientRectToScreenRect(HWND hwnd, const RECT& clientRect);
	};
}
