#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <cstdint>

namespace Engine {

	// front
	class GraphicsCore;
	class AssetDatabase;
	class ECSWorld;

	//============================================================================
	//	SceneSkyboxResolver structures
	//============================================================================
	// シーンから解決したskybox情報
	struct SceneSkyboxInfo {

		// bindlessで参照するcubemapのSRVインデックス
		uint32_t cubemapIndex = 0xFFFFFFFF;
		// 解決元のcubemapアセット
		AssetID cubemapAssetID{};
		// cubemapへ掛ける色
		Color4 color = Color4::White();
		// 拡散IBL環境光の強さ
		float iblIntensity = 1.0f;
		// 有効なskyboxが見つかったか
		bool found = false;
	};

	//============================================================================
	//	SceneSkyboxResolver class
	//	シーン内の有効なskyboxを探してcubemapを解決する
	//============================================================================
	class SceneSkyboxResolver {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SceneSkyboxResolver() = default;
		~SceneSkyboxResolver() = default;

		// 最初に見つかった有効なskyboxのcubemapを解決する
		static SceneSkyboxInfo Resolve(GraphicsCore& graphicsCore, AssetDatabase* assetDatabase, ECSWorld* world);
	};
} // Engine