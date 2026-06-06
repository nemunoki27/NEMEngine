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

	// アウトラインを出す対象範囲(可視性)
	enum class ScreenSpaceOutlineVisibilityMode : uint8_t {

		// 画面に見えている表面のみ
		VisibleOnly,
	};
	// アウトライン領域の決定方式
	enum class ScreenSpaceOutlineRegionMode : uint8_t {

		// 画面に見えている全シルエットの外周。
		// 遮蔽物によってVisible Maskに開いた穴の縁も描画する既存挙動
		AllVisibleSilhouettes,

		// 選択メッシュ本来の投影範囲より外側の輪郭を優先する。
		// 遮蔽物によってVisible Maskに開いた穴の縁は描画しない
		ExteriorPreferred,
	};

	// 画面空間アウトライン(Mask + Dilation + Composite)描画。Scene保存対象のRuntime Component
	struct ScreenSpaceOutlineComponent {

		// 有効か
		bool enabled = true;

		// アウトラインの色
		Color4 color = Color4::FromHex(0xFF8A00FF);

		// 線の太さ(画面ピクセル)。負値は0へ、NaN/Infは既定値へ丸める
		float widthPixels = 3.0f;

		// 表示方式
		ScreenSpaceOutlineVisibilityMode visibilityMode = ScreenSpaceOutlineVisibilityMode::VisibleOnly;
		// 領域方式
		ScreenSpaceOutlineRegionMode regionMode = ScreenSpaceOutlineRegionMode::AllVisibleSilhouettes;

		// 重なり時の優先順位。値が大きいほど手前(外側)に見える
		int32_t priority = 100;
	};

	// json変換
	void from_json(const nlohmann::json& in, ScreenSpaceOutlineComponent& component);
	void to_json(nlohmann::json& out, const ScreenSpaceOutlineComponent& component);

	ENGINE_REGISTER_COMPONENT(ScreenSpaceOutlineComponent, "ScreenSpaceOutline");
} // Engine
