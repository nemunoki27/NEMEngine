#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	ScreenSpaceOutlineComponent struct
	//============================================================================

	// アウトラインを出す対象範囲
	enum class ScreenSpaceOutlineVisibilityMode :
		uint8_t {

		// 画面に見えている表面のみ
		VisibleOnly,
	};
	// アウトライン領域の決定方式
	enum class ScreenSpaceOutlineRegionMode :
		uint8_t {

		// 画面に見えている全シルエットの外周
		AllVisibleSilhouettes,

		// 選択メッシュ本来の投影範囲より外側の輪郭を優先する
		// 遮蔽物によってマスクに開いた穴の縁は描画しない
		ExteriorPreferred,
	};
	// 2D Rendererの輪郭抽出に使用するAlpha
	enum class ScreenSpaceOutlineAlphaSource :
		uint8_t {

		// ベースカラーテクスチャのAlpha
		TextureColor,

		// 通常描画結果のAlpha
		OutputColor,
	};
	// 2D UIとの重なり方
	enum class ScreenSpaceOutlineUIOcclusionMode :
		uint8_t {

		// 対象の描画順に合成し、後続UIで隠す
		RespectRenderOrder,

		// 全UI描画後に合成して常に手前へ出す
		AlwaysVisible,
	};

	// 画面空間アウトライン
	struct ScreenSpaceOutlineComponent {

		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;

		// 有効か
		bool enabled = true;

		// アウトラインの色
		Color4 color = Color4::FromHex(0xFF8A00FF);

		// 線の太さ
		float widthPixels = 3.0f;

		// 表示方式
		ScreenSpaceOutlineVisibilityMode visibilityMode = ScreenSpaceOutlineVisibilityMode::VisibleOnly;
		// 領域方式
		ScreenSpaceOutlineRegionMode regionMode = ScreenSpaceOutlineRegionMode::AllVisibleSilhouettes;
		// 2D RendererのAlpha判定元
		ScreenSpaceOutlineAlphaSource alphaSource = ScreenSpaceOutlineAlphaSource::OutputColor;
		// 2D UIとの重なり方
		ScreenSpaceOutlineUIOcclusionMode uiOcclusionMode =
			ScreenSpaceOutlineUIOcclusionMode::RespectRenderOrder;

		// 重なり時の優先順位で値が大きいほど手前に見える
		int32_t priority = 100;
	};

	// json変換
	void from_json(const nlohmann::json& in, ScreenSpaceOutlineComponent& component);
	void to_json(nlohmann::json& out, const ScreenSpaceOutlineComponent& component);

} // Engine
