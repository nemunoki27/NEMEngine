#include "TestSelection.h"
#include "TestSuite.h"

int main(int argc, char* argv[]) {

	// 指定された検証と通常の全体検証を切り替える
	if (const auto selected = NEMTests::RunSelectedTests(argc, argv)) { return *selected; }
	return NEMTests::RunAllTests();
}
