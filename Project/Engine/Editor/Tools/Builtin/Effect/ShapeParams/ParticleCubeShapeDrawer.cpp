#include "ParticleCubeShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

//============================================================================
//	ParticleCubeShapeDrawer classMethods
//============================================================================
bool Engine::ParticleCubeShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	return MyGUI::DragVector3("大きさ", group.cube.size, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
}
