#include "ParticleCubeShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleCubeShapeDrawer classMethods
//============================================================================
bool Engine::ParticleCubeShapeDrawer::DrawImGui(ParticleEffectAsset& asset) const {

	return MyGUI::DragVector3("大きさ", asset.cube.size, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
}
