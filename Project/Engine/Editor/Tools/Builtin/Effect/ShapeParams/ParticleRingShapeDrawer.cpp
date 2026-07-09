#include "ParticleRingShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleRingShapeDrawer classMethods
//============================================================================
bool Engine::ParticleRingShapeDrawer::DrawImGui(ParticleEffectAsset& asset) const {

	bool changed = false;
	changed |= MyGUI::DragFloat("外周半径", asset.ring.outerRadius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("内周半径", asset.ring.innerRadius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("開始角", asset.ring.startAngle, ParticleGui::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragFloat("終了角", asset.ring.endAngle, ParticleGui::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragInt("分割数", asset.ring.divide).valueChanged;
	return changed;
}
