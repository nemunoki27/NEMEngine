#include "ParticleCylinderShapeDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

//============================================================================
//	ParticleCylinderShapeDrawer classMethods
//============================================================================
bool Engine::ParticleCylinderShapeDrawer::DrawImGui(ParticleEffectGroup& group) const {

	bool changed = false;
	changed |= MyGUI::DragFloat("上面半径", group.cylinder.topRadius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("中心半径", group.cylinder.centerRadius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("下面半径", group.cylinder.bottomRadius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("上面Weight", group.cylinder.topRadiusWeight, ParticleGUI::MakeDragSetting(0.0f, 1.0f)).valueChanged;
	changed |= MyGUI::DragFloat("下面Weight", group.cylinder.bottomRadiusWeight, ParticleGUI::MakeDragSetting(0.0f, 1.0f)).valueChanged;
	changed |= MyGUI::ColorEdit("上面色", group.cylinder.topColor).valueChanged;
	changed |= MyGUI::ColorEdit("中心色", group.cylinder.centerColor).valueChanged;
	changed |= MyGUI::ColorEdit("底面色", group.cylinder.bottomColor).valueChanged;
	changed |= MyGUI::DragFloat("高さ", group.cylinder.height, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("展開角", group.cylinder.maxAngle, ParticleGUI::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragInt("円周分割", group.cylinder.radialDivide,
		{ .minValue = 3,.maxValue = kMaxPrimitiveDivide }).valueChanged;
	changed |= MyGUI::DragInt("高さ分割", group.cylinder.heightDivide,
		{ .minValue = 2,.maxValue = kMaxPrimitiveDivide }).valueChanged;
	changed |= MyGUI::EnumCombo("フタ", group.cylinder.cap).valueChanged;
	changed |= MyGUI::EnumCombo("UVモード", group.cylinder.uvMode).valueChanged;
	return changed;
}
