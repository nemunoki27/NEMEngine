#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
// c++
#include <exception>
#include <iostream>

// 実行名と例外を表示して検証結果を返す
bool NEMTests::RunTest(const char* name, const std::function<bool()>& test) {

	// 異常終了した場合も実行中の検証名を残す
	std::cout << "[RUN] " << name << std::endl;
	try {
		const bool passed = test();
		if (!passed) {
			std::cerr << "[FAIL] " << name << '\n';
		}
		return passed;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
		return false;
	}
}
