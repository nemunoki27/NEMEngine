#include "ParticleSphereShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleSphereShapeDrawer classMethods
//============================================================================
bool Engine::ParticleSphereShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	return MyGUI::DragFloat("半径", group.sphere.radius, ParticleGui::MakeDragSetting(0.001f, 10000.0f)).valueChanged;
}
