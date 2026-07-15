#include "ParticleCubeShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleCubeShapeDrawer classMethods
//============================================================================
bool Engine::ParticleCubeShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	return MyGUI::DragVector3("大きさ", group.cube.size, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
}
