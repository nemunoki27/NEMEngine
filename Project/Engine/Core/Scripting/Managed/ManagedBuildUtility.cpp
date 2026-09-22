#include "ManagedBuildUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <ctime>

namespace Engine::ManagedBuildUtility {

std::string ToUtf8Path(const std::filesystem::path& path) {
		return Engine::Algorithm::ConvertString(path.wstring());
	}

std::wstring Widen(const std::string& text) {
		return Engine::Algorithm::ConvertString(text);
	}

std::string BuildProfile() {
		return _PROFILE;
	}

double DurationMs(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end) {
		return std::chrono::duration<double, std::milli>(end - begin).count();
	}

std::string NowTimeStringUtf8() {
		const std::time_t now = std::time(nullptr);
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		char buffer[32]{};
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
		return std::string(buffer);
	}
}
