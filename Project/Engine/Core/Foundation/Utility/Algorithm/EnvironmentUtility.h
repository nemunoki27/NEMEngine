#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>

namespace Engine::Algorithm {

	// 現在のプロセス環境を取得し未設定と空値を区別する
	bool TryReadProcessEnvironment(const std::wstring& name, std::wstring& value, bool& exists);
}
