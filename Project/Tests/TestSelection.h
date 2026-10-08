#pragma once

// c++
#include <optional>

namespace NEMTests {

	// 引数で指定された検証だけを実行する
	std::optional<int> RunSelectedTests(int argc, char* argv[]);
}
