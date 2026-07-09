#include "ParticleHemisphereShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleHemisphereShapeDrawer classMethods
//============================================================================
bool Engine::ParticleHemisphereShapeDrawer::DrawImGui(ParticleEffectAsset& asset) const {

	return MyGUI::DragFloat("半径", asset.hemisphere.radius, ParticleGui::MakeDragSetting(0.001f, 10000.0f)).valueChanged;
}
