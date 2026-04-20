#pragma once

//============================================================================
//	include
//============================================================================

// windows
#include <windows.h>
// c++
#include <crtdbg.h>
#include <iostream>
#include <string>
#include <cassert>
#include <sstream>
#include <source_location>

namespace Engine {

	//============================================================================
	//	Assert class
	//	自作アサートクラス
	//============================================================================
	class Assert {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		Assert() = default;
		~Assert() = default;

		// ビルド設定に応じて呼び出し
		static void Call(bool condition, const std::string& message, const std::source_location& location = std::source_location::current());
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// デバッグ時: 条件NGならメッセージを出力しデバッガ停止(_Assert::Call_EXPR)
		static void DebugAssert(bool condition, const std::string& message, const std::source_location& location);
		// リリース時: 条件NGならクリティカルログを出力しプロセス終了
		static void ReleaseAssert(bool condition, const std::string& message, const std::source_location& location);
	};
}