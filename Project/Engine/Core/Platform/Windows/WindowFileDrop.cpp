#include "WindowFileDrop.h"

//============================================================================
//	include
//============================================================================
// windows
#include <shellapi.h>

using namespace Engine;

WindowFileDrop WindowFileDrop::Read(HWND hwnd, WPARAM wparam) {

	HDROP drop = reinterpret_cast<HDROP>(wparam);
	POINT dropPoint{};
	DragQueryPoint(drop, &dropPoint);
	ClientToScreen(hwnd, &dropPoint);

	const UINT fileCount = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
	std::vector<std::string> paths;
	paths.reserve(fileCount);
	for (UINT i = 0; i < fileCount; ++i) {

		const UINT length = DragQueryFileW(drop, i, nullptr, 0);
		if (length == 0) {
			continue;
		}
		std::wstring wide(static_cast<size_t>(length) + 1, L'\0');
		const UINT copiedLength = DragQueryFileW(drop, i, wide.data(), length + 1);
		if (copiedLength == 0) {
			continue;
		}
		wide.resize(copiedLength);

		const int utf8Size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
			static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		if (utf8Size <= 0) {
			continue;
		}
		std::string utf8(static_cast<size_t>(utf8Size), '\0');
		if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
			static_cast<int>(wide.size()), utf8.data(), utf8Size, nullptr, nullptr) != utf8Size) {

			continue;
		}
		paths.emplace_back(std::move(utf8));
	}
	DragFinish(drop);

	return { std::move(paths), Vector2(static_cast<float>(dropPoint.x), static_cast<float>(dropPoint.y)) };
}
