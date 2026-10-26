#include "ShaderGraphCompileFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>

// c++
#include <fstream>
#include <utility>

NEMTests::ShaderGraphCompileFixture::ShaderGraphCompileFixture(
	std::filesystem::path generatedRoot, std::filesystem::path shaderRoot)
	: generatedRoot_(std::move(generatedRoot)), shaderRoot_(std::move(shaderRoot)) {
}

bool NEMTests::ShaderGraphCompileFixture::WriteGeneratedGraph(
	const Engine::ShaderGraphAsset& sourceGraph, std::string_view name) const {

	// 各検証の生成先を分ける
	std::error_code ec;

	const std::filesystem::path graphRoot = generatedRoot_ / std::string(name);
	std::filesystem::create_directories(graphRoot, ec);
	if (ec) {
		return false;
	}
	const std::filesystem::path surfacePath = graphRoot / "surface.hlsli";
	const std::filesystem::path opaquePath = graphRoot / "opaque.PS.hlsl";
	const std::filesystem::path transparentPath = graphRoot / "transparent.PS.hlsl";
	const std::filesystem::path vertexPath = graphRoot / "vertex.VS.hlsl";
	const std::filesystem::path meshPath = graphRoot / "mesh.MS.hlsl";
	const std::filesystem::path rayTracingPath = graphRoot / "rayTracing.RT.hlsl";
	const Engine::ShaderGraphCompileOutput generated = Engine::ShaderGraphCompiler::Compile(sourceGraph, "surface.hlsli");
	if (!generated.Succeeded()) {
		return false;
	}
	auto write = [](const std::filesystem::path& path, std::string_view source) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream.write(source.data(), static_cast<std::streamsize>(source.size()));
		return stream.good();
	};
	if (!write(surfacePath, generated.surfaceHLSL) || !write(opaquePath, generated.opaquePixelHLSL) ||
		!write(transparentPath, generated.transparentPixelHLSL)) {

		return false;
	}
	if ((!generated.vertexHLSL.empty() && !write(vertexPath, generated.vertexHLSL)) ||
		(!generated.meshHLSL.empty() && !write(meshPath, generated.meshHLSL)) ||
		(!generated.particleGPUPixelHLSL.empty() && !write(graphRoot / "particleGPU.PS.hlsl", generated.particleGPUPixelHLSL)) ||
		(!generated.rayTracingHLSL.empty() && !write(rayTracingPath, generated.rayTracingHLSL))) {

		return false;
	}
	if ((!generated.depthPixelHLSL.empty() && !write(graphRoot / "depth.PS.hlsl", generated.depthPixelHLSL)) ||
		(!generated.pickingPixelHLSL.empty() && !write(graphRoot / "picking.PS.hlsl", generated.pickingPixelHLSL))) {

		return false;
	}
	if (!generated.particleGPUPixelHLSL.empty()) {
		Engine::DxShaderCompiler compiler;
		compiler.Init();
		if (!compiler.CompileShader((graphRoot / "particleGPU.PS.hlsl").wstring(), L"ps_6_0", L"main",
			Engine::ShaderStage::PS).IsValid()) return false;
	}
	return true;
}

// 頂点とPixelの定数衝突を実際のコンパイルで確認する
bool NEMTests::ShaderGraphCompileFixture::CompileRasterShaders(std::string_view name) const {

	Engine::DxShaderCompiler compiler;
	compiler.Init();
	const auto root = generatedRoot_ / std::string(name);
	for (const char* file : { "opaque.PS.hlsl", "transparent.PS.hlsl" }) {
		const wchar_t* entry = std::string_view(file) == "transparent.PS.hlsl" ? L"mainTransparent" : L"main";
		if (!compiler.CompileShader((root / file).wstring(), L"ps_6_6", entry, Engine::ShaderStage::PS).IsValid()) return false;
	}
	return compiler.CompileShader((root / "vertex.VS.hlsl").wstring(), L"vs_6_6", L"main", Engine::ShaderStage::VS).IsValid() &&
		compiler.CompileShader((root / "mesh.MS.hlsl").wstring(), L"ms_6_6", L"main", Engine::ShaderStage::MS).IsValid();
}
