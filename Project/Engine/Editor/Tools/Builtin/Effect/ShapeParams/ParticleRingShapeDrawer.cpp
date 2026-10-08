#include "ParticleRingShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

//============================================================================
//	ParticleRingShapeDrawer classMethods
//============================================================================
bool Engine::ParticleRingShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	bool changed = false;
	changed |= MyGUI::DragFloat("外周半径", group.ring.outerRadius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("内周半径", group.ring.innerRadius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("開始角", group.ring.startAngle, ParticleGUI::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragFloat("終了角", group.ring.endAngle, ParticleGUI::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragInt("分割数", group.ring.divide).valueChanged;
	return changed;
}
