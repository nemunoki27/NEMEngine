#include "ShaderCookInputTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Core/ShaderSourceIncludeHandler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <fstream>

bool NEMTests::TestShaderCookInputs() {

	using namespace Engine;
	TestDirectory directory("ShaderCookInputs", RuntimePaths::GetSavedPath("Tests"));
	const auto root = directory.GetPath() / L"入力";
	std::filesystem::create_directories(root);
	const auto source = root / "main.PS.hlsl";
	const auto outside = directory.GetPath() / "outside.hlsli";
	{ std::ofstream file(root / "inside.hlsli"); file << "float4 GetColor() { return float4(1, 0, 0, 1); }\n"; }
	{ std::ofstream file(outside); file << "float4 GetColor() { return float4(0, 1, 0, 1); }\n"; }
	{ std::ofstream file(source); file << "#include \"inside.hlsli\"\nfloat4 main() : SV_Target { return GetColor(); }\n"; }
	if (!IsShaderSourceWithinRoot(source, root) || IsShaderSourceWithinRoot(outside, root) ||
		IsShaderSourceWithinRoot(root / ".." / "outside.hlsli", root)) { return false; }
	DxShaderCompiler compiler;
	compiler.Init();
	compiler.SetSourceRoot(root);
	// 配置済みのソースとIncludeは通常どおりコンパイルできる
	if (!compiler.CompileShader(source.wstring(), L"ps_6_0", L"main", ShaderStage::PS).IsValid()) { return false; }
	// 正常な親ソースでも入力外のIncludeは使用できない
	{ std::ofstream file(source); file << "#include \"../outside.hlsli\"\nfloat4 main() : SV_Target { return GetColor(); }\n"; }
	if (compiler.CompileShader(source.wstring(), L"ps_6_0", L"main", ShaderStage::PS).IsValid()) { return false; }
	return !compiler.CompileShader(outside.wstring(), L"ps_6_0", L"main", ShaderStage::PS).IsValid();
}
