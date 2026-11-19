#include "ShaderGraphNodePreviewCases.h"
#include "ShaderGraphNodePreviewGPUFixture.h"
#include "TestImage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
// directX
#include <DirectXTex.h>

using namespace Engine;

bool NEMTests::WriteShaderGraphNodePreviewImages() {

	constexpr uint32_t kImageSize = 128;
	const auto directory = RuntimePaths::GetEngineProjectRoot().parent_path() / "LocalWork/Verification/ShaderGraphVerification/Images";
	std::filesystem::create_directories(directory);
	ShaderGraphNodePreviewGPUFixture fixture;
	// 識別子の異なるGraphでも既定の指数演算を同じ値へ評価する
	for (uint32_t iteration = 0; iteration < 32; ++iteration) {
		const auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Exponential);
		const auto compiled = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID);
		const auto pixels = fixture.Render(compiled);
		if (std::any_of(pixels.begin(), pixels.end(), [](const Vector4& pixel) { return pixel != Vector4(1, 1, 1, 1); })) {
			std::ofstream(directory / "Exponential.PS.hlsl") << compiled.pixelHLSL;
			std::ofstream(directory / "Exponential.graph.json") << ToJson(testCase.graph).dump(2);
			std::cerr << "Exponential default iteration=" << iteration << " result=" << pixels.front().x << ',' <<
				pixels.front().y << ',' << pixels.front().z << '\n';
			return false;
		}
	}
	nlohmann::json catalog = nlohmann::json::array();
	for (const auto& descriptor : ShaderGraphNodeRegistry::GetDescriptors()) {
		if (!descriptor.preview) continue;
		const auto testCase = CreateShaderGraphNodePreviewCase(descriptor.kind);
		const auto count = GetShaderGraphOutputCount(testCase.graph.nodes.front());
		for (uint32_t slot = 0; slot < count; ++slot) {
			ShaderGraphNodePreviewOptions options;
			auto compiled = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID, slot, options, testCase.resolver);
			if (compiled.usesGeometry) {
				options.sphere = true;
				compiled = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID, slot, options, testCase.resolver);
			}
			if (!compiled.Succeeded()) {
				std::cerr << "Preview image compilation: " << descriptor.name << " output=" << slot << '\n';
				return false;
			}
			auto pixels = fixture.Render(compiled, 1.25f, .02f, .03f, nullptr, kImageSize, {0, 0, 0, 0});
			if (descriptor.kind == ShaderGraphNodeKind::Exponential &&
				std::any_of(pixels.begin(), pixels.end(), [](const Vector4& pixel) { return pixel != Vector4(1, 1, 1, 1); })) {
				std::ofstream(directory / "Exponential.PS.hlsl") << compiled.pixelHLSL;
				std::cerr << "Exponential default result=" << pixels.front().x << ',' << pixels.front().y << ',' << pixels.front().z << '\n';
				return false;
			}
			if (pixels.size() != kImageSize * kImageSize || std::any_of(pixels.begin(), pixels.end(), [](const Vector4& pixel) {
				return !std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(pixel.z) ||
					!std::isfinite(pixel.w) || pixel.x < 0 || 1 < pixel.x || pixel.y < 0 || 1 < pixel.y ||
					pixel.z < 0 || 1 < pixel.z || pixel.w < 0 || 1 < pixel.w;
			})) return false;
			const std::string file = std::to_string(static_cast<uint32_t>(descriptor.kind)) + "-" + std::to_string(slot) + ".png";
			const DirectX::Image image{kImageSize, kImageSize, DXGI_FORMAT_R32G32B32A32_FLOAT,
				kImageSize * sizeof(Vector4), pixels.size() * sizeof(Vector4),
				reinterpret_cast<uint8_t*>(pixels.data())};
			if (!SaveTestImage(image, directory / file)) return false;
			catalog.push_back({{"name", descriptor.name}, {"output", slot}, {"file", file}, {"sphere", options.sphere}});
		}
	}
	std::ofstream manifest(directory / "catalog.json", std::ios::binary);
	manifest << catalog.dump(2);
	if (!manifest) return false;
	std::cout << "Node preview images: " << catalog.size() << " outputs, 128x128, " << directory << '\n';
	return fixture.CheckMessages();
}
