#include "ParticleFlipbookModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookFrame.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookTileLayout.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	ParticleFlipbookModule classMethods
//============================================================================
void Engine::ParticleFlipbookModule::FromJson(const nlohmann::json& params) {

	ReadFlipbookTileLayout(params, settings_.tilesX, settings_.tilesY);
	settings_.cycles = params.value("cycles", settings_.cycles);
}

nlohmann::json Engine::ParticleFlipbookModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();

	WriteFlipbookTileLayout(params, settings_.tilesX, settings_.tilesY);
	params["cycles"] = settings_.cycles;
	return params;
}

void Engine::ParticleFlipbookModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	// 寿命
	float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);

	// アニメーションループ
	float cycleT = std::fmod(progress * (std::max)(settings_.cycles, 0.0f), 1.0f);

	// 寿命終了時に先頭フレームへ戻るのを防ぐ
	if (progress >= 1.0f && settings_.cycles > 0.0f) {
		cycleT = std::nextafter(1.0f, 0.0f);
	}

	// フリップブックのUV値を計算
	FlipbookFrame frame = CalcFlipbookFrame(settings_.tilesX, settings_.tilesY, cycleT);

	particle.uvScale = frame.uvScale;
	particle.uvOffset = frame.uvOffset;
}
