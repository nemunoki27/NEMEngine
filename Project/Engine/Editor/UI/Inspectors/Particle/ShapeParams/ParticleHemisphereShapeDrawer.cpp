#include "ParticleHemisphereShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Particle/GUI/ParticleGUIHelpers.h>

//============================================================================
//	ParticleHemisphereShapeDrawer classMethods
//============================================================================
bool Engine::ParticleHemisphereShapeDrawer::DrawImGui(ParticleSystemSettings& group) const {

	return MyGUI::DragFloat("半径", group.hemisphere.radius, ParticleGUI::MakeDragSetting(0.001f, 10000.0f)).valueChanged;
}
