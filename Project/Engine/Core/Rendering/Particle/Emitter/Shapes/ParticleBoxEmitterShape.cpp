#include "ParticleBoxEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>

//============================================================================
//	ParticleBoxEmitterShape classMethods
//============================================================================
void Engine::ParticleBoxEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	if (const auto it = data.find("boxSize"); it != data.end()) { settings.box.size = Vector3::FromJson(*it); }
	settings.box.facePosX = data.value("boxFacePosX", settings.box.facePosX);
	settings.box.faceNegX = data.value("boxFaceNegX", settings.box.faceNegX);
	settings.box.facePosY = data.value("boxFacePosY", settings.box.facePosY);
	settings.box.faceNegY = data.value("boxFaceNegY", settings.box.faceNegY);
	settings.box.facePosZ = data.value("boxFacePosZ", settings.box.facePosZ);
	settings.box.faceNegZ = data.value("boxFaceNegZ", settings.box.faceNegZ);
}

void Engine::ParticleBoxEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["boxSize"] = settings.box.size.ToJson();
	data["boxFacePosX"] = settings.box.facePosX;
	data["boxFaceNegX"] = settings.box.faceNegX;
	data["boxFacePosY"] = settings.box.facePosY;
	data["boxFaceNegY"] = settings.box.faceNegY;
	data["boxFacePosZ"] = settings.box.facePosZ;
	data["boxFaceNegZ"] = settings.box.faceNegZ;
}

void Engine::ParticleBoxEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	// 有効な面からランダムに選び、面上の点から面法線方向へ飛ばす
	const bool faces[6] = {
		settings.box.facePosX, settings.box.faceNegX, settings.box.facePosY,
		settings.box.faceNegY, settings.box.facePosZ, settings.box.faceNegZ };
	int32_t enabledCount = 0;
	for (bool face : faces) { enabledCount += face ? 1 : 0; }
	if (enabledCount == 0) {
		return;
	}
	int32_t pick = RandomGenerator::Generate(0, enabledCount - 1);
	int32_t faceIndex = 0;
	for (int32_t i = 0; i < 6; ++i) {
		if (faces[i] && pick-- == 0) { faceIndex = i; break; }
	}
	const Vector3 half = settings.box.size * 0.5f;
	position = RandomGenerator::Generate(-half, half);
	switch (faceIndex) {
	case 0: position.x = half.x; direction = Vector3(1.0f, 0.0f, 0.0f); break;
	case 1: position.x = -half.x; direction = Vector3(-1.0f, 0.0f, 0.0f); break;
	case 2: position.y = half.y; direction = Vector3(0.0f, 1.0f, 0.0f); break;
	case 3: position.y = -half.y; direction = Vector3(0.0f, -1.0f, 0.0f); break;
	case 4: position.z = half.z; direction = Vector3(0.0f, 0.0f, 1.0f); break;
	case 5: position.z = -half.z; direction = Vector3(0.0f, 0.0f, -1.0f); break;
	}
}

void Engine::ParticleBoxEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, [[maybe_unused]] bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	renderer->DrawOBB(center, settings.box.size * 0.5f, rotation, Color4::Red());
#endif
}

bool Engine::ParticleBoxEmitterShape::DrawImGui(ParticleEmitterSettings& settings) const {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	changed |= MyGUI::DragVector3("大きさ", settings.box.size, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::Checkbox("+X面", settings.box.facePosX);
	changed |= MyGUI::Checkbox("-X面", settings.box.faceNegX);
	ImGui::Spacing();
	changed |= MyGUI::Checkbox("+Y面", settings.box.facePosY);
	changed |= MyGUI::Checkbox("-Y面", settings.box.faceNegY);
	ImGui::Spacing();
	changed |= MyGUI::Checkbox("+Z面", settings.box.facePosZ);
	changed |= MyGUI::Checkbox("-Z面", settings.box.faceNegZ);
	return changed;
#else
	(void)settings;
	return false;
#endif
}
