#include "ParticleRectEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

//============================================================================
//	ParticleRectEmitterShape classMethods
//============================================================================
void Engine::ParticleRectEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	if (const auto it = data.find("rectSize"); it != data.end()) { settings.rect.size = Vector2::FromJson(*it); }
	settings.rect.edgePosX = data.value("rectEdgePosX", settings.rect.edgePosX);
	settings.rect.edgeNegX = data.value("rectEdgeNegX", settings.rect.edgeNegX);
	settings.rect.edgePosY = data.value("rectEdgePosY", settings.rect.edgePosY);
	settings.rect.edgeNegY = data.value("rectEdgeNegY", settings.rect.edgeNegY);
}

void Engine::ParticleRectEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["rectSize"] = settings.rect.size.ToJson();
	data["rectEdgePosX"] = settings.rect.edgePosX;
	data["rectEdgeNegX"] = settings.rect.edgeNegX;
	data["rectEdgePosY"] = settings.rect.edgePosY;
	data["rectEdgeNegY"] = settings.rect.edgeNegY;
}

void Engine::ParticleRectEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	// 有効な辺からランダムに選び、辺上の点から辺法線方向へ飛ばす
	const bool edges[4] = {
		settings.rect.edgePosX, settings.rect.edgeNegX, settings.rect.edgePosY, settings.rect.edgeNegY };
	int32_t enabledCount = 0;
	for (bool edge : edges) { enabledCount += edge ? 1 : 0; }
	if (enabledCount == 0) {
		return;
	}
	int32_t pick = RandomGenerator::Generate(0, enabledCount - 1);
	int32_t edgeIndex = 0;
	for (int32_t i = 0; i < 4; ++i) {
		if (edges[i] && pick-- == 0) { edgeIndex = i; break; }
	}
	const Vector2 half = settings.rect.size * 0.5f;
	position = Vector3(RandomGenerator::Generate(-half.x, half.x),
		RandomGenerator::Generate(-half.y, half.y), 0.0f);
	switch (edgeIndex) {
	case 0: position.x = half.x; direction = Vector3(1.0f, 0.0f, 0.0f); break;
	case 1: position.x = -half.x; direction = Vector3(-1.0f, 0.0f, 0.0f); break;
	case 2: position.y = half.y; direction = Vector3(0.0f, 1.0f, 0.0f); break;
	case 3: position.y = -half.y; direction = Vector3(0.0f, -1.0f, 0.0f); break;
	}
}

void Engine::ParticleRectEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
	const Color4 color = Color4::Red();

	// 矩形の外周を線で表す
	const Vector2 half = settings.rect.size * 0.5f;
	const Vector3 corners[4] = {
		Vector3(-half.x, -half.y, 0.0f), Vector3(half.x, -half.y, 0.0f),
		Vector3(half.x, half.y, 0.0f), Vector3(-half.x, half.y, 0.0f) };

	// 2Dはスクリーン空間の2Dレンダラーで描く
	if (is2D) {

		LineRenderer2D* renderer2D = LineRenderer::GetInstance()->Get2D();
		if (!renderer2D) {
			return;
		}
		// ローカル点をエンティティの回転と位置でスクリーン座標へ変換する
		auto toScreen = [&](const Vector3& local) {
			const Vector3 world = center + Vector3::Transform(local, rotationMatrix);
			return Vector2(world.x, world.y);
			};
		for (int32_t i = 0; i < 4; ++i) {
			renderer2D->DrawLine(toScreen(corners[i]), toScreen(corners[(i + 1) % 4]), color);
		}
		return;
	}

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	for (int32_t i = 0; i < 4; ++i) {
		renderer->DrawLine(center + Vector3::Transform(corners[i], rotationMatrix),
			center + Vector3::Transform(corners[(i + 1) % 4], rotationMatrix), color);
	}
#endif
}

bool Engine::ParticleRectEmitterShape::DrawImGui(ParticleEmitterSettings& settings) const {

	bool changed = false;
	changed |= MyGUI::DragVector2("大きさ", settings.rect.size, ParticleGui::MakeDragSetting(0.0f, 100000.0f)).valueChanged;
	changed |= MyGUI::Checkbox("+X辺", settings.rect.edgePosX);
	ImGui::SameLine();
	changed |= MyGUI::Checkbox("-X辺", settings.rect.edgeNegX);
	changed |= MyGUI::Checkbox("+Y辺", settings.rect.edgePosY);
	ImGui::SameLine();
	changed |= MyGUI::Checkbox("-Y辺", settings.rect.edgeNegY);
	return changed;
}
