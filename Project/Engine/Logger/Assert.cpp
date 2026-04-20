#include "Assert.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Logger.h>
#include <Engine/Utility/Algorithm/Algorithm.h>

//============================================================================
//	Assert classMethods
//============================================================================

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