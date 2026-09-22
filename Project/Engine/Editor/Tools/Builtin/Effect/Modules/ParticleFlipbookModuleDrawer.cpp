#include "ParticleFlipbookModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleFlipbookModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookTileLayout.h>

using namespace Engine;

bool ParticleFlipbookModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleFlipbookModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	if (MyGUI::DragInt("分割Y", settings.tilesY, { .minValue = 1, .maxValue = 256 }).valueChanged) {

		settings.tilesY = (std::max)(settings.tilesY, 1);
		changed = true;
	}
	NormalizeFlipbookTileLayout(settings.tilesX, settings.tilesY);

	// 縦の分割数だけ、Xタイルの数を設定する
	for (int32_t index = 0; index < settings.tilesY; ++index) {

		// ラベル
		std::string label = "分割X: " + std::to_string(index);
		ImGui::PushID(label.c_str());

		int32_t& tileX = settings.tilesX[index];
		if (MyGUI::DragInt(label.c_str(), tileX,
			{ .minValue = 1, .maxValue = 16384 }).valueChanged) {

			tileX = (std::max)(tileX, 1);
			changed = true;
		}
		ImGui::PopID();
	}
	changed |= MyGUI::DragFloat("周回数", settings.cycles, ParticleGUI::MakeDragSetting(0.01f, 100.0f)).valueChanged;
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;
}
