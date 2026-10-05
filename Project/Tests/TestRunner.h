#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <functional>


namespace NEMTests {

	// 実行名と例外を表示して検証結果を返す
	bool RunTest(const char* name, const std::function<bool()>& test);
}
