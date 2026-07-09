#include "ParticleCrossPlaneShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleCrossPlaneShapeDrawer classMethods
//============================================================================
bool Engine::ParticleCrossPlaneShapeDrawer::DrawImGui(ParticleEffectAsset& asset) const {

	bool changed = false;
	changed |= MyGUI::DragVector2("大きさ", asset.crossPlane.size, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragInt("枚数", asset.crossPlane.planeCount).valueChanged;
	return changed;
}
