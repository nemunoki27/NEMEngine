#include "ParticleCylinderShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleCylinderShapeDrawer classMethods
//============================================================================
bool Engine::ParticleCylinderShapeDrawer::DrawImGui(ParticleEffectAsset& asset) const {

	bool changed = false;
	changed |= MyGUI::DragFloat("上面半径", asset.cylinder.topRadius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("下面半径", asset.cylinder.bottomRadius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("高さ", asset.cylinder.height, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("展開角", asset.cylinder.maxAngle, ParticleGui::MakeDragSetting(0.0f, 10.0f)).valueChanged;
	changed |= MyGUI::DragInt("円周分割", asset.cylinder.radialDivide).valueChanged;
	return changed;
}
