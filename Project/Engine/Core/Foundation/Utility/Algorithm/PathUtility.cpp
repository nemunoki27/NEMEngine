#include "PathUtility.h"

//============================================================================
//	include
//============================================================================
#include "UTFConversion.h"

#include <stdexcept>
#include <limits>
#include <Windows.h>

namespace Engine::Algorithm {

	std::filesystem::path PathFromUTF8(const std::string& path) {

		if (path.empty()) {
			return {};
		}

		// OSへ渡す長さと終端を検証する
		if (path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) || path.find('\0') != std::string::npos) {
			throw std::invalid_argument("Invalid path length or embedded null");
		}
		const int sizeNeeded = ::MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			path.data(),
			static_cast<int>(path.size()),
			nullptr,
			0
			);
		if (sizeNeeded == 0) {
			throw std::runtime_error("MultiByteToWideChar failed.");
		}

		std::wstring result(static_cast<size_t>(sizeNeeded), L'\0');
		if (::MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			path.data(),
			static_cast<int>(path.size()),
			result.data(),
			sizeNeeded) == 0) {

			throw std::runtime_error("MultiByteToWideChar failed.");
		}
		return std::filesystem::path(result);
	}

	std::string PathToUTF8(const std::filesystem::path& path) {

		if (path.native().find(L'\0') != std::wstring::npos) {
			throw std::invalid_argument("Path contains an embedded null");
		}
		return ConvertString(path.wstring());
	}
}
