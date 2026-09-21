#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/WindowFileDrop.h>

// c++
#include <cstring>
#include <iostream>
#include <string>
// windows
#include <shellapi.h>
#include <shlobj.h>

bool TestWindowFileDropConversion() {

	std::wstring paths = L"C:\\Tests\\画像.png";
	paths.push_back(L'\0');
	paths += L"C:\\Tests\\Mesh.fbx";
	paths.push_back(L'\0');
	// 不正なUTF文字列を含むファイルだけを除外する
	paths.push_back(static_cast<wchar_t>(0xD800));
	paths.push_back(L'\0');
	paths.push_back(L'\0');
	const size_t size = sizeof(DROPFILES) + paths.size() * sizeof(wchar_t);
	HGLOBAL handle = GlobalAlloc(GHND, size);
	if (!handle) {
		return false;
	}
	void* memory = GlobalLock(handle);
	if (!memory) {
		GlobalFree(handle);
		return false;
	}
	auto* header = static_cast<DROPFILES*>(memory);
	header->pFiles = sizeof(DROPFILES);
	header->fWide = TRUE;
	std::memcpy(static_cast<char*>(memory) + sizeof(DROPFILES), paths.data(), paths.size() * sizeof(wchar_t));
	GlobalUnlock(handle);

	const auto drop = Engine::WindowFileDrop::Read(nullptr, reinterpret_cast<WPARAM>(handle));
	const bool passed = drop.paths == std::vector<std::string>{ "C:\\Tests\\画像.png", "C:\\Tests\\Mesh.fbx" };
	if (!passed) {
		std::cerr << "External file drop conversion failed\n";
	}
	return passed;
}
