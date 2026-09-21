#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <string>
#include <vector>
// windows
#include <Windows.h>

namespace Engine {

	struct WindowFileDrop {

		std::vector<std::string> paths;
		Vector2 position;

		// OSのドロップを変換してハンドルを解放する
		static WindowFileDrop Read(HWND hwnd, WPARAM wparam);
	};
}
