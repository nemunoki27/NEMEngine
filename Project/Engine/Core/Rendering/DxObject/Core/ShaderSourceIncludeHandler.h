#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <filesystem>
#include <map>
// directX
#include <dxcapi.h>

namespace Engine {

	using ShaderSourceDependencies = std::map<std::filesystem::path, std::string>;

	// Shaderソースが指定された入力root内にあるか確認する
	bool IsShaderSourceWithinRoot(const std::filesystem::path& source, const std::filesystem::path& root);
	// Cook入力外のファイルを読まないIncludeHandlerを作成する
	ComPtr<IDxcIncludeHandler> CreateShaderSourceIncludeHandler(IDxcIncludeHandler* handler, const std::filesystem::path& root,
		ShaderSourceDependencies* dependencies = nullptr);
}
