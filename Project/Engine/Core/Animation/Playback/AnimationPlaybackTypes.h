#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>

// c++
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	AnimationPlaybackTypes enum class
	//============================================================================
	// クリップ終端での扱い
	enum class AnimationWrapMode :
		uint8_t {

		UseClip,      // クリップ自身のloop設定に従う
		Once,         // 一度だけ再生し終端で停止
		Loop,         // ループ再生
		PingPong,     // 往復再生
	};

	//============================================================================
	//	AnimationPlaybackTypes struct
	//============================================================================
	// 名前付きで再生するクリップ1つ分の設定
	struct AnimationState {

		std::string name;
		AssetID clip{};
		float speed = 1.0f;
		float weight = 1.0f;
		int32_t priority = 0;
		bool additive = false;
		AnimationWrapMode wrapMode = AnimationWrapMode::UseClip;
		// 向き相対、再生開始時の姿勢を正面として位置/回転を相対適用する(クリップ側設定を上書き)
		bool relativeTransform = false;
		// ループの繋ぎ補間、ループ時のみ有効(クリップ側設定を上書き)
		AnimationLoopBridgeSettings loopBridge{};
		// ループ回数、0=無限、N=N回で終端保持、Loop再生時のみ参照
		int32_t loopCount = 0;
		// 往復回数、0=無限、1往復(行って戻る)=1回、PingPong再生時のみ参照
		int32_t pingPongCount = 0;
		// 開始遅延(秒)、グループ再生時にこの時間だけ待ってから再生を始める
		float startDelay = 0.0f;
		// ループ/往復のインターバル(秒)、繋ぎ補間の後、次の再生までの待機時間
		float interval = 0.0f;
	};

	// クリップ再生の進行フェーズ
	enum class AnimationClipPhase :
		uint8_t {

		Play,     // クリップ本編を再生中
		Bridge,   // ループの繋ぎ補間中
		Interval, // 次の再生までの待機中
	};

	// 同時再生するクリップの束、Playはこのグループ名で行う
	struct AnimationGroup {

		std::string name;
		// グループに属するクリップ設定
		std::vector<AnimationState> states;
	};

	AnimationGroup LoadAnimationGroup(const nlohmann::json& in);
	nlohmann::json SaveAnimationGroup(const AnimationGroup& group);
}
