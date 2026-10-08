#include "EnvironmentUtility.h"

//============================================================================
//	include
//============================================================================
// c++
#include <utility>
#include <vector>
// windows
#include <Windows.h>

//============================================================================
//	EnvironmentUtility functions
//============================================================================
namespace Engine::Algorithm {

	bool TryReadProcessEnvironment(const std::wstring& name, std::wstring& value, bool& exists) {

		// OSへ渡せない変数名を除外
		if (name.empty() || name.find(L'\0') != std::wstring::npos || name.find(L'=') != std::wstring::npos) {
			return false;
		}
		std::vector<wchar_t> buffer;
		for (;;) {

			// 未設定と空値の結果を分ける
			::SetLastError(ERROR_SUCCESS);
			const DWORD length = ::GetEnvironmentVariableW(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0) {
				const DWORD error = ::GetLastError();
				if (error != ERROR_SUCCESS && error != ERROR_ENVVAR_NOT_FOUND) {
					return false;
				}
				value.clear();
				exists = error == ERROR_SUCCESS;
				return true;
			}
			// 取得中に値が伸びた場合も容量を更新
			if (length >= buffer.size()) {
				buffer.resize(length);
				continue;
			}
			std::wstring result(buffer.data(), length);
			value = std::move(result);
			exists = true;
			return true;
		}
	}
}
