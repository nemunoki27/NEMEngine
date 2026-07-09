#include "ParticlePlaneShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticlePlaneShapeDrawer classMethods
//============================================================================
bool Engine::ParticlePlaneShapeDrawer::DrawImGui(ParticleEffectAsset& asset) const {

	return MyGUI::DragVector2("大きさ", asset.plane.size, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
}
