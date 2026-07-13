#include "ParticleFlipbookModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookFrame.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookTileLayout.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	ParticleFlipbookModule classMethods
//============================================================================
void Engine::ParticleFlipbookModule::FromJson(const nlohmann::json& params) {

	ReadFlipbookTileLayout(params, tilesX_, tilesY_);
	cycles_ = params.value("cycles", cycles_);
}

nlohmann::json Engine::ParticleFlipbookModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();

	WriteFlipbookTileLayout(params, tilesX_, tilesY_);
	params["cycles"] = cycles_;
	return params;
}

void Engine::ParticleFlipbookModule::OnUpdate(std::span<Particle> alive, [[maybe_unused]] float deltaTime) {

	for (Particle& particle : alive) {

		// 寿命
		float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);

		// アニメーションループ
		float cycleT = std::fmod(progress * (std::max)(cycles_, 0.0f), 1.0f);

		// 寿命終了時に先頭フレームへ戻るのを防ぐ
		if (progress >= 1.0f && cycles_ > 0.0f) {
			cycleT = std::nextafter(1.0f, 0.0f);
		}

		// フリップブックのUV値を計算
		FlipbookFrame frame = CalcFlipbookFrame(tilesX_, tilesY_, cycleT);

		particle.uvScale = frame.uvScale;
		particle.uvOffset = frame.uvOffset;
	}
}

bool Engine::ParticleFlipbookModule::DrawImGui() {

	bool changed = false;
	if (MyGUI::DragInt("分割Y", tilesY_, { .minValue = 1, .maxValue = 256 }).valueChanged) {

		tilesY_ = (std::max)(tilesY_, 1);
		changed = true;
	}
	NormalizeFlipbookTileLayout(tilesX_, tilesY_);

	// 縦の分割数だけ、Xタイルの数を設定する
	for (int32_t index = 0; index < tilesY_; ++index) {

		// ラベル
		std::string label = "分割X: " + std::to_string(index);
		ImGui::PushID(label.c_str());

		int32_t& tileX = tilesX_[index];
		if (MyGUI::DragInt(label.c_str(), tileX,
			{ .minValue = 1, .maxValue = 16384 }).valueChanged) {

			tileX = (std::max)(tileX, 1);
			changed = true;
		}
		ImGui::PopID();
	}
	changed |= MyGUI::DragFloat("周回数", cycles_, ParticleGui::MakeDragSetting(0.01f, 100.0f)).valueChanged;
	return changed;
}
