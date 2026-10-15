#pragma once

#include <filesystem>

namespace Engine {
	class GraphicsCore;
	class AssetDatabase;
	struct BakedLightingData;
}

namespace NEMTests {

	bool TestBakedLightingCPU();
	bool TestBakedLightingGPU();
	bool TestBakedLightingRuntimeGPU(Engine::GraphicsCore& graphics, Engine::AssetDatabase& database,
		const Engine::BakedLightingData& data, const std::filesystem::path& directory);
}
