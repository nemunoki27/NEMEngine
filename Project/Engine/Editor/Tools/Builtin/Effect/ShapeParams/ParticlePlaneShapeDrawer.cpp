#include "ParticlePlaneShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticlePlaneShapeDrawer classMethods
//============================================================================
bool Engine::ParticlePlaneShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	bool changed = false;
	changed |= MyGUI::EnumCombo("面タイプ", group.plane.axis).valueChanged;
	changed |= MyGUI::DragVector2("大きさ", group.plane.size, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	return changed;
}
