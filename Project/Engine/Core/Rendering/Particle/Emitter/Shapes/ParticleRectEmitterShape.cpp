#include "ParticleRectEmitterShape.h"

//============================================================================
//	include
//============================================================================
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
