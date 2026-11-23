#pragma once

//============================================================================
//	include
//============================================================================
#include <Windows.h>

// c++
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace NEMGameBootstrap {

	inline int PrintNotices() {

		const HMODULE module = ::GetModuleHandleW(nullptr);
		const HRSRC resource = ::FindResourceW(module, L"NEM_PRODUCT_NOTICES", MAKEINTRESOURCEW(10));
		const DWORD size = resource ? ::SizeofResource(module, resource) : 0;
		const void* data = resource ? ::LockResource(::LoadResource(module, resource)) : nullptr;
		if (!data || size == 0) return EXIT_FAILURE;

		HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
		bool ownConsole = false;
		if (!output || output == INVALID_HANDLE_VALUE) {
			if (!::AttachConsole(ATTACH_PARENT_PROCESS)) ownConsole = ::AllocConsole() != FALSE;
			output = ::GetStdHandle(STD_OUTPUT_HANDLE);
		}
		DWORD written = 0;
		DWORD mode = 0;
		if (::GetConsoleMode(output, &mode)) {
			const char* utf8 = static_cast<const char*>(data);
			const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8, size, nullptr, 0);
			if (length == 0) return EXIT_FAILURE;
			std::wstring text(length, L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, utf8, size, text.data(), length);
			for (size_t pos = 0; pos < text.size(); pos += written) {
				const size_t remaining = text.size() - pos;
				const DWORD count = static_cast<DWORD>(remaining < 8192 ? remaining : 8192);
				if (!::WriteConsoleW(output, text.data() + pos, count, &written, nullptr) || !written) return EXIT_FAILURE;
			}
			if (ownConsole) {
				constexpr wchar_t prompt[] = L"\r\nEnter キーで閉じます。";
				::WriteConsoleW(output, prompt, ARRAYSIZE(prompt) - 1, &written, nullptr);
				wchar_t input[2]{};
				::ReadConsoleW(::GetStdHandle(STD_INPUT_HANDLE), input, ARRAYSIZE(input), &written, nullptr);
			}
		} else {
			const auto* bytes = static_cast<const char*>(data);
			for (DWORD pos = 0; pos < size; pos += written) {
				if (!::WriteFile(output, bytes + pos, size - pos, &written, nullptr) || !written) return EXIT_FAILURE;
			}
		}
		return EXIT_SUCCESS;
	}
}

inline int NEM_LaunchGame(std::string_view arguments) {

	const auto first = arguments.find_first_not_of(" \t");
	if (first != std::string_view::npos &&
		arguments.substr(first, arguments.find_last_not_of(" \t") - first + 1) == "--licenses") {
		return NEMGameBootstrap::PrintNotices();
	}

	std::wstring path(32768, L'\0');
	const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (length == 0 || path.size() <= length) {
		return EXIT_FAILURE;
	}
	path.resize(length);
	auto directory = std::filesystem::path(path).parent_path();
	const auto data = directory / L"Data";
	const bool packed = ::GetFileAttributesW((data / L"Content.nempak").c_str()) != INVALID_FILE_ATTRIBUTES;
	const bool product = ::FindResourceW(nullptr, L"NEM_PRODUCT_NOTICES", MAKEINTRESOURCEW(10)) != nullptr;
	if (!packed && (product || ::GetFileAttributesW((data / L"Runtime").c_str()) != INVALID_FILE_ATTRIBUTES)) {
		::MessageBoxW(nullptr, L"Data/Content.nempak が見つかりません。", L"NEMEngine", MB_OK | MB_ICONERROR);
		return EXIT_FAILURE;
	}
	if (packed) {
		directory = data / L"Runtime";
	}

	const auto runtime = directory / L"NEMRuntime.dll";
	const HMODULE library = ::LoadLibraryExW(runtime.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	using RunGame = int(*)();
	const auto runGame = library ? reinterpret_cast<RunGame>(::GetProcAddress(library, "NEM_RunGame")) : nullptr;
	if (!runGame) {
		const DWORD error = ::GetLastError();
		const std::wstring message = L"ゲームランタイムを読み込めません。\n" + runtime.wstring() +
			L"\nWindows エラー " + std::to_wstring(error);
		::MessageBoxW(nullptr, message.c_str(), L"NEMEngine", MB_OK | MB_ICONERROR);
		return EXIT_FAILURE;
	}
	return runGame();
}
