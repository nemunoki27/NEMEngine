#include "ParticleEmitterShapeDrawers.h"

#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

bool Engine::ParticleEmitterShapeDrawers::DrawSphere([[maybe_unused]] ParticleEmitterSettings& settings) {

	bool result = MyGUI::DragFloat("半径", settings.sphere.radius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;

	return result;

}

bool Engine::ParticleEmitterShapeDrawers::DrawHemisphere([[maybe_unused]] ParticleEmitterSettings& settings) {

	return MyGUI::DragFloat("半径", settings.sphere.radius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;

}

bool Engine::ParticleEmitterShapeDrawers::DrawBox([[maybe_unused]] ParticleEmitterSettings& settings) {

	bool changed = false;
	changed |= MyGUI::DragVector3("大きさ", settings.box.size, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::Checkbox("+X面", settings.box.facePosX);
	changed |= MyGUI::Checkbox("-X面", settings.box.faceNegX);
	ImGui::Spacing();
	changed |= MyGUI::Checkbox("+Y面", settings.box.facePosY);
	changed |= MyGUI::Checkbox("-Y面", settings.box.faceNegY);
	ImGui::Spacing();
	changed |= MyGUI::Checkbox("+Z面", settings.box.facePosZ);
	changed |= MyGUI::Checkbox("-Z面", settings.box.faceNegZ);
	return changed;

}

bool Engine::ParticleEmitterShapeDrawers::DrawTorus([[maybe_unused]] ParticleEmitterSettings& settings) {

	bool changed = false;
	changed |= MyGUI::DragFloat("主半径", settings.torus.radius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("管半径", settings.torus.thickness, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	return changed;

}

bool Engine::ParticleEmitterShapeDrawers::DrawCircle([[maybe_unused]] ParticleEmitterSettings& settings) {

	ParticleEmitterCircleParams& circle = settings.circle;
	bool changed = false;
	changed |= MyGUI::DragFloat("半径", circle.radius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("最小角", circle.angleMin, ParticleGUI::MakeDragSetting(-360.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragFloat("最大角", circle.angleMax, ParticleGUI::MakeDragSetting(-360.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::Checkbox("時計回り", circle.clockwise);

	changed |= MyGUI::EnumCombo("発生方法", circle.spawnMode).valueChanged;
	if (circle.spawnMode == ParticleEmitterSpawnMode::Progressive) {
		changed |= MyGUI::DragFloat("ステップ角度", circle.stepAngle, ParticleGUI::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	}

	changed |= MyGUI::EnumCombo("速度の向き", circle.velocityMode).valueChanged;
	if (circle.velocityMode == ParticleEmitterVelocityMode::NextPoint) {
		changed |= MyGUI::Checkbox("折り返しで前の向きを使う", circle.usePrevSegmentDirectionOnWrap);
	}
	return changed;

}

bool Engine::ParticleEmitterShapeDrawers::DrawCone([[maybe_unused]] ParticleEmitterSettings& settings) {

	bool changed = false;
	changed |= MyGUI::DragFloat("開き角", settings.cone.angle, ParticleGUI::MakeDragSetting(0.0f, 89.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragFloat("底面半径", settings.cone.radius, ParticleGUI::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	return changed;

}

bool Engine::ParticleEmitterShapeDrawers::DrawPoint([[maybe_unused]] ParticleEmitterSettings& settings) {

	bool result = MyGUI::DragVector3("射出方向", settings.point.direction, ParticleGUI::MakeDragSetting(-1.0f, 1.0f)).valueChanged;;
	return result;

}

bool Engine::ParticleEmitterShapeDrawers::DrawRect([[maybe_unused]] ParticleEmitterSettings& settings) {

	bool changed = false;
	changed |= MyGUI::DragVector2("大きさ", settings.rect.size, ParticleGUI::MakeDragSetting(0.0f, 100000.0f)).valueChanged;
	changed |= MyGUI::Checkbox("+X辺", settings.rect.edgePosX);
	ImGui::SameLine();
	changed |= MyGUI::Checkbox("-X辺", settings.rect.edgeNegX);
	changed |= MyGUI::Checkbox("+Y辺", settings.rect.edgePosY);
	ImGui::SameLine();
	changed |= MyGUI::Checkbox("-Y辺", settings.rect.edgeNegY);
	return changed;

}

bool Engine::ParticleEmitterShapeDrawers::DrawCone2D([[maybe_unused]] ParticleEmitterSettings& settings) {

	bool changed = false;
	changed |= MyGUI::DragFloat("開き角", settings.cone.angle, ParticleGUI::MakeDragSetting(0.0f, 89.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragFloat("底辺半径", settings.cone.radius, ParticleGUI::MakeDragSetting(0.0f, 100000.0f)).valueChanged;
	return changed;

}
