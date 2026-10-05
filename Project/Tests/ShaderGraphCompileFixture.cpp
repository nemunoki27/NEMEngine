#include "ShaderGraphCompileFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>

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
		(!generated.rayTracingHLSL.empty() && !write(rayTracingPath, generated.rayTracingHLSL))) {

		return false;
	}
	if ((!generated.depthPixelHLSL.empty() && !write(graphRoot / "depth.PS.hlsl", generated.depthPixelHLSL)) ||
		(!generated.pickingPixelHLSL.empty() && !write(graphRoot / "picking.PS.hlsl", generated.pickingPixelHLSL))) {

		return false;
	}
	return true;
}
