#include "BakedLightingTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingBake.h>
#include <Engine/Core/Rendering/Renderer/Lighting/EnvironmentSpecularMap.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace NEMTests {
	Engine::BakedLightingInput MakeBakedLightingTestInput(const std::filesystem::path& directory);
}

bool NEMTests::TestBakedLightingGPU() {

	using namespace Engine;
	TestDirectory directory("BakedLightingGPU", RuntimePaths::GetGameAssetsRoot());
	GraphicsCore graphics;
	graphics.Init(false);
	struct GraphicsShutdown {
		GraphicsCore& graphics;
		~GraphicsShutdown() { graphics.Finalize(); }
	} shutdown{graphics};
	ShowWindow(WinApp::GetHwnd(), SW_HIDE);
	if (!graphics.GetDXObject().GetFeatureController().GetSupport().SupportsInlineRayTracingPath()) {
		std::cout << "Hardware bake skipped: inline ray tracing is unavailable\n";
		return true;
	}
	AssetDatabase database;
	database.Init();
	auto input = MakeBakedLightingTestInput(directory.GetPath());
	input.preparation.data.captures.push_back({Vector3(0.0f, 1.0f, 0.0f), 4.0f, Vector3::AnyInit(4.0f)});
	BakedLightingSettings settings;
	settings.atlasSize = 64;
	settings.sampleCount = 32;
	settings.texelsPerFrame = 8192;
	settings.captureSize = 16;
	settings.probeSpacing = 1.0f;
	auto& baker = graphics.GetLightingBake();
	baker.Start(std::move(input), settings, database);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
	while (baker.IsRunning() && std::chrono::steady_clock::now() < deadline) {
		graphics.GetProcessMessage();
		graphics.TickFrameServices();
		graphics.BeginRenderFrame();
		graphics.Render();
		graphics.SubmitRenderFrame();
		graphics.EndRenderFrame();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const auto result = baker.GetResult();
	if (!result) { std::cerr << "Bake failed: " << baker.GetStatus() << '\n'; return false; }
	double red = 0.0;
	double green = 0.0;
	double blue = 0.0;
	for (const auto& value : result->lightmaps) {
		if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) return false;
		red += value.x;
		green += value.y;
		blue += value.z;
	}
	std::cout << "Baked radiance: " << red << ", " << green << ", " << blue << ", probes=" << result->probes.size() << '\n';
	return red > 1.0 && red > green * 4.0 && red > blue * 4.0 && !result->probes.empty() &&
		result->captures.size() == 1 && result->captures.front().mipCount > 1 && GraphicsFrameState::GetActiveCount() == 2 &&
		TestBakedLightingRuntimeGPU(graphics, database, *result, directory.GetPath());
}
