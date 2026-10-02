#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <cstdint>
// windows
#include <Windows.h>

namespace Engine {

	//============================================================================
	//	WindowPlacement class
	//	全画面表示と復帰用のウィンドウ矩形を管理する
	//============================================================================
	class WindowPlacement {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 全画面表示を切り替える
		void SetFullscreen(HWND hwnd, bool fullscreen);
		bool IsFullscreen() const { return fullscreen_; }
		// 初期ウィンドウの矩形を作成する
		RECT Prepare(uint32_t width, uint32_t height, DWORD style);
		// 製品と同じクライアントサイズへ一時変更する
		bool BeginClientPreview(HWND hwnd, uint32_t width, uint32_t height);
		// 一時変更前の位置・サイズ・最大化状態へ戻す
		void EndClientPreview(HWND hwnd);
		// 対象モニターへ収まる最大クライアントサイズを返す
		Vector2I GetMaximumClientSize(HWND hwnd) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		bool fullscreen_ = false;
		RECT windowRect_{};
		WINDOWPLACEMENT previewPlacement_{ sizeof(WINDOWPLACEMENT) };
		DWORD previewStyle_ = 0;
		DWORD previewExStyle_ = 0;
		bool clientPreview_ = false;
	};
}
