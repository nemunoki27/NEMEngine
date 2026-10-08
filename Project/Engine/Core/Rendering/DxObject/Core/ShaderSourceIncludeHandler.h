#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <filesystem>
// directX
#include <dxcapi.h>

namespace Engine {

	// Shaderソースが指定された入力root内にあるか確認する
	bool IsShaderSourceWithinRoot(const std::filesystem::path& source, const std::filesystem::path& root);
	// Cook入力外のファイルを読まないIncludeHandlerを作成する
	ComPtr<IDxcIncludeHandler> CreateShaderSourceIncludeHandler(IDxcIncludeHandler* handler, const std::filesystem::path& root);
}
