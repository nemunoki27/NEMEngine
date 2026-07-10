#include "ParticleFlipbookModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookFrame.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	ParticleFlipbookModule classMethods
//============================================================================
void Engine::ParticleFlipbookModule::FromJson(const nlohmann::json& params) {

	tilesX_ = (std::max)(params.value("tilesX", tilesX_), 1);
	tilesY_ = (std::max)(params.value("tilesY", tilesY_), 1);
	cycles_ = params.value("cycles", cycles_);
}

nlohmann::json Engine::ParticleFlipbookModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["tilesX"] = tilesX_;
	params["tilesY"] = tilesY_;
	params["cycles"] = cycles_;
	return params;
}

void Engine::ParticleFlipbookModule::OnUpdate(std::span<Particle> alive, [[maybe_unused]] float deltaTime) {

	for (Particle& particle : alive) {

		// 進行度からコマ番号を求めて左上から右下の順に送る
		const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
		const float cycleT = std::fmod(progress * cycles_, 1.0f);
		const FlipbookFrame frame = CalcFlipbookFrame(tilesX_, tilesY_, cycleT);

		particle.uvScale = frame.uvScale;
		particle.uvOffset = frame.uvOffset;
	}
}

bool Engine::ParticleFlipbookModule::DrawImGui() {

	bool changed = false;
	if (MyGUI::DragInt("分割X", tilesX_).valueChanged) {

		tilesX_ = (std::max)(tilesX_, 1);
		changed = true;
	}
	if (MyGUI::DragInt("分割Y", tilesY_).valueChanged) {

		tilesY_ = (std::max)(tilesY_, 1);
		changed = true;
	}
	changed |= MyGUI::DragFloat("周回数", cycles_, ParticleGui::MakeDragSetting(0.01f, 100.0f)).valueChanged;
	return changed;
}
