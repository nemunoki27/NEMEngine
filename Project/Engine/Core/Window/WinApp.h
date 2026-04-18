#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Vector2.h>

// windows
#include <Windows.h>
// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	WinApp class
	//	Windowsウィンドウの生成/メッセージ処理/フルスクリーン切替を担当する。
	//============================================================================
	class WinApp {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		WinApp() = default;
		~WinApp() = default;

		// ウィンドウクラス登録とウィンドウ生成を行う
		void Create(uint32_t sizeX, uint32_t sizeY, const wchar_t* title);

		// メッセージキューを処理し、WM_QUITを検出したらtrueを返す
		bool ProcessMessage();

		//--------- accessor -----------------------------------------------------

		// フルスクリーンの有効/無効を切り替える
		static void SetFullscreen(bool fullscreen);
		// カーソル範囲制限をON/OFF
		static void SetCursorClipEnabled(bool enabled);
		// クライアント座標系の任意矩形でクリップ
		static void ClipCursorToClientRect(const Vector2& size, const Vector2& pos);
		// ウィンドウのクライアント領域にクリップ
		static void ClipCursorToClient();
		// 解除
		static void ReleaseCursorClip();

		// カーソルクリップ範囲のセット
		static void SetCursorClipRect(const Vector2& size, const Vector2& pos);

		// カーソルの表示/非表示
		static void SetCursorVisible(bool visible);
		static bool IsCursorVisible() { return cursorVisible_; }

		// 現在のウィンドウハンドル(HWND)を返す
		static HWND GetHwnd() { return hwnd_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		static HWND hwnd_;

		static bool cursorClipEnabled_;
		static bool useCustomClipRect_;
		static RECT customClientClipRect_;

		static bool cursorVisible_;

		UINT windowStyle_;
		static RECT windowRect_;

		//--------- functions ----------------------------------------------------

		// カーソルの強制表示/非表示
		static void ForceShowCursor(bool show);
		// 必要に応じてカーソルの表示/非表示を行う
		static void ApplyCursorVisibilityIfNeeded();

		// 必要に応じてカーソルのクリッピングを行う
		static void ApplyCursorClipIfNeeded();

		// クライアント領域のRECTをスクリーン座標のRECTに変換する
		static RECT ClientRectToScreenRect(HWND hwnd, const RECT& clientRect);

		// Win32のウィンドウプロシージャ
		static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

		// ウィンドウクラスを登録する
		void RegisterWindowClass();
	};
}; // Engine