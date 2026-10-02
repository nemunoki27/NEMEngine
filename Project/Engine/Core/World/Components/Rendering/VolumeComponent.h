#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	VolumeComponent structure
	//	Camera位置から画面効果Profileを選別する領域
	//============================================================================
	struct VolumeComponent {

		AssetID profile{};
		Vector3 size = Vector3::AnyInit(10.0f);
		float priority = 0.0f;
		float weight = 1.0f;
		float blendDistance = 0.0f;
		uint32_t layerMask = 1u;
		bool global = true;
		bool enabled = true;
	};

	// JSON変換
	void from_json(const nlohmann::json& in, VolumeComponent& component);
	void to_json(nlohmann::json& out, const VolumeComponent& component);
} // Engine
