#include "ParticleCrossPlaneShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

//============================================================================
//	ParticleCrossPlaneShapeDrawer classMethods
//============================================================================
bool Engine::ParticleCrossPlaneShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	bool changed = false;
	changed |= MyGUI::DragVector2("大きさ", group.crossPlane.size, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragInt("枚数", group.crossPlane.planeCount).valueChanged;
	return changed;
}
