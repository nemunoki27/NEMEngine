#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	FlipbookAnimationComponent struct
	//============================================================================
	// 連番画像アニメーションの再生
	struct FlipbookAnimationComponent {
	
		// アニメーションの有効/無効
		bool enabled = true;
		// ループ再生するか
		bool loop = true;
		// 次のループ再生までの待機時間
		float loopInterval = 0.0f;
		// 編集中でもプレビュー再生するか
		bool playInEditMode = true;
		// 再生終了後、何も表示されないようにするか
		bool endAnimUnDisplay = false;
		// テクスチャの分割数
		// 行ごとの横タイル数
		std::vector<int32_t> tilesX{ 1 };
		// 縦タイル数
		int32_t tilesY = 1;
		// 再生にかかる時間
		float duration = 1.0f;
		// イージング
		EasingType easingType = EasingType::EaseInSine;

		// 再生中か
		bool runtimePlaying = false;
		// アニメーションが終了したか
		bool runtimeAnimationFinished = false;
		// ループ再生した回数
		int32_t runtimeRepeatCount = 0;
		// 経過時間
		float runtimeElapsed = 0.0f;
	};

	// json適用
	void from_json(const nlohmann::json& in, FlipbookAnimationComponent& component);
	void to_json(nlohmann::json& out, const FlipbookAnimationComponent& component);

	ENGINE_REGISTER_COMPONENT(FlipbookAnimationComponent, "FlipbookAnimation");
} // Engine
