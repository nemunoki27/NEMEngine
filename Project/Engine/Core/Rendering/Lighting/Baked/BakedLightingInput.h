#pragma once

//============================================================================
//	include
//============================================================================
#include "BakedLightingGeometry.h"
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/LightGPUTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;

	struct BakedLightingInput {

		BakedLightingPreparation preparation;
		std::vector<DirectionalLightGPU> directionalLights;
		std::vector<PointLightGPU> pointLights;
		std::vector<SpotLightGPU> spotLights;
		std::vector<RectLightGPU> rectLights;
		AssetID skyboxTexture{};
		Color4 skyboxColor = Color4::White();
		float skyIntensity = 1.0f;
		std::vector<std::pair<AssetID, std::filesystem::path>> assets;
	};

	//============================================================================
	//	BakedLightingInputBuilder namespace
	//	保存済みSceneからベイク入力を確定する
	//============================================================================
	namespace BakedLightingInputBuilder {

		// Worldを保持しない入力へ変換する
		BakedLightingInput Capture(AssetDatabase& database, ECSWorld& world, AssetID sceneAsset, UUID sceneInstance);
		// 固定ライトと環境の変更を照合する
		nlohmann::json LightingState(ECSWorld& world, AssetID sceneAsset, UUID sceneInstance = {});
		// 固定Rendererの保存値を取得する
		std::string ReceiverSettings(const ECSWorld& world, const Entity& entity);
		// 固定背景とライトが同じ保存値を持つか確認する
		bool IsCurrent(const BakedLightingData& data, ECSWorld& world, UUID sceneInstance);
		// 入力Assetの内容とインポート設定を照合する
		void ValidateAssets(const BakedLightingData& data, const AssetDatabase& database, bool readSourceFiles);
	}
}
