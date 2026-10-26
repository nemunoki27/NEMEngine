#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// json
#include <json.hpp>

namespace Engine {

	// 粒子の深度フェードと表面照明
	struct ParticleSurfaceSettings {

		bool softParticles = false;
		float softDistance = 0.2f;
		bool lit = false;
		bool receiveShadows = true;
		bool receiveIBL = true;
		AssetID normalTexture{};
		float normalScale = 1.0f;
		float metallic = 0.0f;
		float roughness = 0.5f;
	};

	// 表面設定を保存する
	void to_json(nlohmann::json& out, const ParticleSurfaceSettings& value);
	// 保存した表面設定を読む
	void from_json(const nlohmann::json& in, ParticleSurfaceSettings& value);
}
