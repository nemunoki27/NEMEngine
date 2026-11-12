#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	//============================================================================
	//	LightShadowExclusion structure
	//	ライトごとに影から除外する参照
	//============================================================================
	struct LightShadowExclusion {

		SceneEntityReference target{};
		bool includeChildren = false;
		UUID runtimeTargetID{};
	};

	//============================================================================
	//	LightInfluenceSettings structure
	//	照射対象と影の除外を独立して保持する
	//============================================================================
	struct LightInfluenceSettings {

		uint32_t renderingLayerMask = 0x00FFFFFFu;
		uint32_t shadowExcludedLayerMask = 0;
		std::vector<LightShadowExclusion> shadowExclusions;
	};

	// 保存用参照と実行時の参照から対象を解決する
	Entity ResolveLightShadowExclusion(const ECSWorld& world, Entity owner, const LightShadowExclusion& exclusion);
	// 子孫を含めた除外対象を重複なしで解決する
	std::vector<Entity> ResolveLightShadowExclusions(const ECSWorld& world, Entity owner, const LightInfluenceSettings& settings);
	// レイヤーまたは個別指定で影から除外されるか
	bool IsLightShadowExcluded(const LightInfluenceSettings& settings, std::span<const Entity> exclusions,
		Entity target, uint32_t renderingLayerMask);
	// json変換
	void from_json(const nlohmann::json& in, LightShadowExclusion& exclusion);
	void to_json(nlohmann::json& out, const LightShadowExclusion& exclusion);
	void from_json(const nlohmann::json& in, LightInfluenceSettings& settings);
	void to_json(nlohmann::json& out, const LightInfluenceSettings& settings);
}
