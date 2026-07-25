#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	InvertedHullOutlineComponent struct
	//============================================================================
	// アウトラインの膨張方式
	enum class OutlineExpansionMode : uint8_t {

		// 法線方向へ膨張させる
		NormalDirection,
		// ピボットからの放射方向へ膨張させる
		PositionScaling,
	};
	// アウトライン幅の指定方式
	enum class OutlineWidthMode : uint8_t {

		// モデル空間距離
		ModelUnits,
		// 画面ピクセル数
		ScreenPixels,
	};

	// 背面法アウトライン描画
	struct InvertedHullOutlineComponent {

		// 有効か
		bool enabled = true;

		// ModelUnitsではモデル空間距離、ScreenPixelsでは画面ピクセル数
		float width = 0.01f;
		// アウトラインの色
		Color4 color = Color4::Black();

		// 膨張方式
		OutlineExpansionMode expansionMode = OutlineExpansionMode::NormalDirection;
		// 幅モード
		OutlineWidthMode widthMode = OutlineWidthMode::ModelUnits;

		// 正値でカメラから奥へ押し込む
		float cameraZOffset = 0.0f;

		// RGBにobject/local-space normalを[0,1]エンコードしたLinear texture
		bool useBakedNormal = false;
		AssetID bakedNormalTexture{};

		// Rチャンネルは0で膨張なし、1でwidthをそのまま適用する
		// 部位別アウトライン幅と線の抑制に使用するLinear texture
		bool useOutlineSampler = false;
		AssetID outlineSamplerTexture{};

		// trueの場合、同一フレームのoutlined silhouetteをstencilへ書き込み、
		// Hull描画時にNOT_EQUALで内部や重なりを抑制する
		bool useStencil = false;
	};

	// json変換
	void from_json(const nlohmann::json& in, InvertedHullOutlineComponent& component);
	void to_json(nlohmann::json& out, const InvertedHullOutlineComponent& component);

} // Engine
