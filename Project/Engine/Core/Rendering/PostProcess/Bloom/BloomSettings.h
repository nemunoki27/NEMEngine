#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetSizing.h>

// c++
#include <vector>

namespace Engine {

	// HDRの輝度抽出と光の広がり
	struct BloomSettings {

		float intensity = 0.6f; // 合成する光の強度
		float threshold = 1.0f; // 抽出するシーン輝度
		float softKnee = 0.5f; // 閾値の前後をつなぐ幅
		float scatter = 0.7f; // 低解像度の光を重ねる割合
		Color4 tint = Color4::White(); // 光に掛ける色
	};

	// 保存値へ実行中の変更を重ねて有効範囲へ収める
	BloomSettings ReadBloomSettings(const MaterialParameterSet& parameters, const MaterialParameterSet* overrides = nullptr);
	// 同じ名前と型で編集値を保存する
	void WriteBloomSettings(MaterialParameterSet& parameters, const BloomSettings& settings);
	// 縮小先の実寸を最大5段階まで求める
	std::vector<RenderTargetSize> BuildBloomLevelSizes(uint32_t width, uint32_t height);
}
