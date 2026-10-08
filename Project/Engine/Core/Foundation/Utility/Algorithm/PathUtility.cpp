#include "PathUtility.h"

//============================================================================
//	include
//============================================================================
#include "UTFConversion.h"
#include "EnvironmentUtility.h"

// c++
#include <stdexcept>
#include <limits>
#include <vector>

// windows
#include <Windows.h>

namespace Engine::Algorithm {

	std::filesystem::path GetEnvironmentPath(const std::wstring& name) {

		// 現在のプロセス環境からパスを取得
		std::wstring value;
		bool exists = false;
		if (!TryReadProcessEnvironment(name, value, exists) || !exists) {
			return {};
		}
		return std::filesystem::path(value);
	}

	std::filesystem::path GetExecutablePath() {

		// 長い配置パスも切り詰めずに取得
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;) {

			const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0) {
				return {};
			}
			if (length < buffer.size()) {
				return std::filesystem::path(buffer.data(), buffer.data() + length);
			}
			// 異常な必要量で確保を繰り返さない
			if (buffer.size() >= (1u << 20)) {
				return {};
			}
			buffer.resize(buffer.size() * 2);
		}
	}

	std::filesystem::path ToFileSystemPath(const std::filesystem::path& path) {

		if (path.empty()) {
			return {};
		}
		if (path.native().find(L'\0') != std::wstring::npos || path.native().starts_with(L"\\\\.\\")) {
			throw std::invalid_argument("ファイルのパスが不正です");
		}
		// 区切りと親階層を解決してから長いパスへ変換する
		auto absolute = std::filesystem::absolute(path).lexically_normal();
		absolute.make_preferred();
		const auto& name = absolute.native();
		if (name.starts_with(L"\\\\?\\")) {
			return absolute;
		}
		if (name.starts_with(L"\\\\")) {
			return std::filesystem::path(L"\\\\?\\UNC\\" + name.substr(2));
		}
		return std::filesystem::path(L"\\\\?\\" + name);
	}

	std::filesystem::path PathFromUTF8(const std::string& path) {

		if (path.empty()) {
			return {};
		}

		// OSへ渡す長さと終端を検証する
		if (path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) || path.find('\0') != std::string::npos) {
			throw std::invalid_argument("パスの長さまたは終端が不正です");
		}
		// 厳密な文字列変換でOSのパスへ渡す
		return std::filesystem::path(ConvertStringStrict(path));
	}

	std::string PathToUTF8(const std::filesystem::path& path) {

		if (path.native().find(L'\0') != std::wstring::npos) {
			throw std::invalid_argument("パスに途中の終端文字が含まれています");
		}
		// OSの文字列をUTF8へ変換
		return ConvertString(path.wstring());
	}
}
