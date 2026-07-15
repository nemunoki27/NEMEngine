#include "ParticleCylinderShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleCylinderShapeDrawer classMethods
//============================================================================
bool Engine::ParticleCylinderShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	bool changed = false;
	changed |= MyGUI::DragFloat("上面半径", group.cylinder.topRadius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("下面半径", group.cylinder.bottomRadius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("高さ", group.cylinder.height, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("展開角", group.cylinder.maxAngle, ParticleGui::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragInt("円周分割", group.cylinder.radialDivide).valueChanged;
	return changed;
}
