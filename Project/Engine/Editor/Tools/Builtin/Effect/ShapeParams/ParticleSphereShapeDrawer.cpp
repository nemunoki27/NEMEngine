#include "ParticleSphereShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

//============================================================================
//	ParticleSphereShapeDrawer classMethods
//============================================================================
bool Engine::ParticleSphereShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	return MyGUI::DragFloat("半径", group.sphere.radius, ParticleGUI::MakeDragSetting(0.001f, 10000.0f)).valueChanged;
}
