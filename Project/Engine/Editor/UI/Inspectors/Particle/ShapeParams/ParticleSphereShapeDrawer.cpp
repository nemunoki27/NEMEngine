#include "ParticleSphereShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Particle/GUI/ParticleGUIHelpers.h>

//============================================================================
//	ParticleSphereShapeDrawer classMethods
//============================================================================
bool Engine::ParticleSphereShapeDrawer::DrawImGui(ParticleSystemSettings& group) const {

	return MyGUI::DragFloat("半径", group.sphere.radius, ParticleGUI::MakeDragSetting(0.001f, 10000.0f)).valueChanged;
}
