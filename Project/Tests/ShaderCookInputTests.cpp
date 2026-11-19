#include "ShaderCookInputTests.h"
#include "ShaderGraphCompileFixture.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Core/ShaderSourceIncludeHandler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>

// c++
#include <fstream>
#include <future>
#include <algorithm>
#include <vector>

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
	// 深い相対IncludeをCookとEditorの両方で解決する
	std::filesystem::create_directory(root / "nested");
	std::string include;
	for (int index = 0; index < 32; ++index) { include += "nested/../"; }
	{ std::ofstream file(source); file << "#include \"" << include << "inside.hlsli\"\nfloat4 main() : SV_Target { return GetColor(); }\n"; }
	if (!compiler.CompileShader(source.wstring(), L"ps_6_0", L"main", ShaderStage::PS).IsValid()) { return false; }
	compiler.SetSourceRoot({});
	if (!compiler.CompileShader(source.wstring(), L"ps_6_0", L"main", ShaderStage::PS).IsValid()) { return false; }
	compiler.SetSourceRoot(root);
	// 正常な親ソースでも入力外のIncludeは使用できない
	{ std::ofstream file(source); file << "#include \"../outside.hlsli\"\nfloat4 main() : SV_Target { return GetColor(); }\n"; }
	if (compiler.CompileShader(source.wstring(), L"ps_6_0", L"main", ShaderStage::PS).IsValid()) { return false; }
	return !compiler.CompileShader(outside.wstring(), L"ps_6_0", L"main", ShaderStage::PS).IsValid();
}

bool NEMTests::TestShaderCache(unsigned expectedWidth) {

	using namespace Engine;
	DxShaderCompiler compiler;
	compiler.Init();
	const auto source = (RuntimePaths::GetGameAssetsRoot() / "Probe/cache.CS.hlsl").wstring();
	// 同じShaderを並行要求し、全呼出元が同じReflectionを取得できることを確認する
	std::vector<std::future<bool>> workers;
	std::promise<void> start;
	const auto ready = start.get_future().share();
	for (unsigned i = 0; i < 4; ++i) {
		workers.emplace_back(std::async(std::launch::async, [&, ready] {
			ready.wait();
			const auto shader = compiler.CompileShader(source, L"cs_6_0", L"main", ShaderStage::CS);
			return shader.IsValid() && shader.reflection.threadGroupX == expectedWidth &&
				shader.reflection.resources.size() == 1 && shader.reflection.resources.front().name == "outputBuffer";
		}));
	}
	start.set_value();
	for (auto& worker : workers) {
		if (!worker.get()) return false;
	}
	return true;
}

bool NEMTests::TestShaderGraphCache() {

	using namespace Engine;
	TestDirectory directory("ShaderGraphCache");
	ShaderGraphCompileFixture fixture(directory.GetPath(), RuntimePaths::GetEngineAssetPath("Rendering"));
	auto graph = CreateDefaultSurfaceShaderGraph("Color");
	DxShaderCompiler compiler;
	compiler.Init();
	const auto root = directory.GetPath() / "Color";
	const auto compile = [&] {
		auto shader = compiler.CompileShader((root / "opaque.PS.hlsl").wstring(), L"ps_6_0", L"main", ShaderStage::PS);
		if (!shader.IsValid()) return std::string{};
		return ContentHash::SHA256({static_cast<const uint8_t*>(shader.GetBytecodePointer()), shader.GetBytecodeSize()});
	};

	if (!fixture.WriteGeneratedGraph(graph, "Color")) return false;
	const auto surface = ContentHash::FileSHA256(root / "surface.hlsli");
	const auto pixel = ContentHash::FileSHA256(root / "opaque.PS.hlsl");
	const auto original = compile();
	if (original.empty() || original != compile()) return false;

	// Base Colorの編集はPixelの入口を変えず、参照先のSurfaceだけを更新する
	const auto input = std::find_if(graph.links.begin(), graph.links.end(), [&](const auto& link) {
		return link.inputNode == graph.outputNode && link.inputSlot == 0;
	});
	if (input == graph.links.end()) return false;
	const auto color = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto& node) {
		return node.id == input->outputNode;
	});
	if (color == graph.nodes.end()) return false;
	color->value.value = Color4(0.15f, 0.45f, 0.8f, 1.0f);
	if (!fixture.WriteGeneratedGraph(graph, "Color") || surface == ContentHash::FileSHA256(root / "surface.hlsli") ||
		pixel != ContentHash::FileSHA256(root / "opaque.PS.hlsl")) return false;
	const auto edited = compile();
	return !edited.empty() && original != edited && edited == compile();
}
