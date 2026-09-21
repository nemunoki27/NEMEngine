#include "PathUtility.h"

//============================================================================
//	include
//============================================================================
#include "UTFConversion.h"

#include <stdexcept>
#include <Windows.h>

namespace Engine::Algorithm {

	std::filesystem::path PathFromUTF8(const std::string& path) {

		if (path.empty()) {
			return {};
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

		return ConvertString(path.wstring());
	}
}
