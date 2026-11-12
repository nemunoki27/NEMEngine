#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <string>
#include <vector>
#include <unordered_set>

// windows
#include <Windows.h>

namespace Engine {

	//============================================================================
	//	InputWindowEvents class
	//	文字入力とfocusと未消費dropを保持する
	//============================================================================
	class InputWindowEvents {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 今frameの文字入力を確定
		void CommitText();
		void PushDroppedFiles(const std::vector<std::string>& paths, const Vector2& screenPoint);
		bool TakeDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint);
		bool PeekDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) const;

		// 入力を受け付けるWindowを登録する
		void RegisterWindow(HWND hwnd);
		// 破棄するWindowの登録とフォーカスを解除する
		void UnregisterWindow(HWND hwnd);
		// 登録されたWindow間でフォーカスを移す
		void SetFocusedWindow(HWND hwnd);

		//--------- accessor -----------------------------------------------------

		const std::string& FrameTextInput() const { return frameText_; }
		bool HasWindowFocus() const { return hasFocus_; }
		void AppendTextInputUtf16(wchar_t code) { pendingWide_.push_back(code); }
		HWND GetFocusedWindow() const { return focusedWindow_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::wstring pendingWide_;
		std::string frameText_;
		bool hasFocus_ = true;
		std::unordered_set<HWND> windows_;
		HWND focusedWindow_ = nullptr;
		std::vector<std::string> droppedFiles_{};
		Vector2 droppedFilesPoint_{};
		bool hasDroppedFiles_ = false;
	};
}
