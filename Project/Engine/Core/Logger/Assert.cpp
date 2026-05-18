#include "Assert.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Logger/Logger.h>
#include <Engine/Core/Utility/Algorithm/Algorithm.h>

//============================================================================
//	Assert classMethods
//============================================================================

namespace {

	void (*g_preAssertHandler)() = nullptr;
	bool g_callingPreAssertHandler = false;
}

void Assert::SetPreAssertHandler(void (*handler)()) {

	g_preAssertHandler = handler;
}

void Assert::InvokePreAssertHandler() {

	if (!g_preAssertHandler || g_callingPreAssertHandler) {
		return;
	}

	g_callingPreAssertHandler = true;
	g_preAssertHandler();
	g_callingPreAssertHandler = false;
}

void Assert::DebugAssert(bool condition, const std::string& message, const std::source_location& location) {

	if (condition) {
		return;
	}

	// メッセージ書き込む
	std::ostringstream oss;
	oss << "[Assert::Call] " << message << "\n  Function: " << location.function_name()
		<< "\n  File: " << location.file_name() << ":" << location.line();

	// ログ出力
	const std::string text = oss.str();
	Logger::Output(LogType::Engine, spdlog::level::critical, "{}", text);

	// 停止前に、必要なら編集中シーンを保存する
	InvokePreAssertHandler();

	// デバッガ停止
#if defined(UNICODE) || defined(_UNICODE)
	const std::wstring wideText = Algorithm::ConvertString(text);
	_ASSERT_EXPR(false, wideText.c_str());
#else
	_ASSERT_EXPR(false, text.c_str());
#endif
}

void Assert::ReleaseAssert(bool condition, const std::string& message, const std::source_location& location) {

	if (condition) {
		return;
	}

	// メッセージ書き込む
	std::ostringstream oss{};
	oss << "[Assert::Call] " << message << " | Function: " << location.function_name() <<
		" | File: " << location.file_name() << ":" << location.line();

	// ログ出力
	Logger::Output(LogType::Engine, spdlog::level::critical, "{}", oss.str());

	// 終了前に、必要なら編集中シーンを保存する
	InvokePreAssertHandler();

	// プログラム強制終了
	std::exit(EXIT_FAILURE);
}

void Assert::Call(bool condition, const std::string& message, const std::source_location& location) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	DebugAssert(condition, message, location);
#else

	ReleaseAssert(condition, message, location);
#endif
}
