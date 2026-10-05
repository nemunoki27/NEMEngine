#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Playback/AnimationPlaybackTypes.h>
#include <Engine/Core/Animation/Controllers/AnimationControllerAsset.h>

// c++
#include <cstdint>
#include <string>
#include <limits>
#include <vector>

namespace Engine {

	// グループ内クリップ1つ分の再生状態
	struct AnimationClipRuntime {

		// グループ内のどのstateか
		std::string stateName;
		// クリップ内時間
		float time = 0.0f;
		// 待機と繋ぎ補間を除いた本編の進行量
		double normalizedTime = 0.0;
		// PingPong用の進行方向
		int8_t dir = 1;
		// loop/pingpong完了回数
		int32_t repeatCount = 0;
		// 残り開始遅延(秒)、0以下で再生開始
		float delayRemaining = 0.0f;
		// 現在の進行フェーズと、Bridge/Interval内の経過時間
		AnimationClipPhase phase = AnimationClipPhase::Play;
		float phaseTime = 0.0f;
		// 遅延を終えて再生を開始したか
		bool started = false;
		// 時間進行中か
		bool playing = false;
		// 非ループ終端へ達したか
		bool finished = false;
		// 終端poseを一度だけ書き込む
		bool terminalPosePending = false;
		// 次の走査に再生開始地点のEventを含める
		bool eventStartPending = true;
		bool sampled = false;
	};

	struct AnimationPlayerComponent {

		// 有効か
		bool enabled = true;
		// アニメーショングループ一覧、各グループが同時再生するクリップを束ねる
		std::vector<AnimationGroup> groups;
		// 開始時に再生するグループ名
		std::string defaultGroup;
		// Play開始時にdefaultGroupを自動再生するか
		bool playOnStart = true;
		// Editモードでもプレビュー再生するか
		bool playInEditMode = false;
		// 全体の再生速度倍率
		float globalSpeed = 1.0f;
		AssetID controller{};

		AssetID runtimeControllerAsset{};
		uint64_t runtimeControllerGeneration = 0;
		AnimationControllerRuntime runtimeController;
		std::vector<AnimationGroup> runtimeControllerGroups;
		bool runtimeControllerStopped = false;
		AnimationGroup runtimeDirectGroup;

		// 再生中グループのクリップごとの再生状態、同時再生の実体
		std::string runtimeCurrentGroup;
		std::vector<AnimationClipRuntime> runtimeCurrentClips;
		// クロスフェード元グループ
		std::string runtimeFromGroup;
		std::vector<AnimationClipRuntime> runtimeFromClips;
		// クロスフェードの進行0-1と所要時間
		float runtimeFade = 0.0f;
		float runtimeFadeDuration = 0.0f;
		// クロスフェード中か
		bool runtimeInTransition = false;

		// 再生中グループ名
		std::string runtimeCurrent;
		// 代表クリップのloop/pingpong完了回数
		int32_t runtimeRepeatCount = 0;
		// いずれかのクリップが再生中か
		bool runtimePlaying = false;
		// 全クリップが非ループ終端へ達したか
		bool runtimeFinished = false;
		// playOnStartを消費済みか
		bool runtimeStarted = false;
		// base値を捕捉済みか
		bool runtimeBaseCaptured = false;
		uint64_t runtimeClipRevision = 0;
		// 再生開始時に捕捉した全プロパティのbase値
		std::vector<AnimationPreviewBaseValue> runtimeBaseValues;

		// C#からの再生要求(グループ名)、systemが立ち上がりで消費する
		std::string runtimePlayRequest;
		float runtimePlayFade = 0.0f;
		AssetID runtimeDirectClipRequest{};
		bool runtimeNormalizedFade = false;
		bool runtimeNormalizedOffset = true;
		float runtimePlayTime = -std::numeric_limits<float>::infinity();
		bool runtimeSeekRequested = false;
		float runtimeNormalizedTimeValue = 0.0f;
		float runtimeClipDuration = 0.0f;
		bool runtimeLooping = false;
		// C#からの停止要求
		bool runtimeStopRequest = false;
	};

	// json適用
	void from_json(const nlohmann::json& in, AnimationPlayerComponent& component);
	void to_json(nlohmann::json& out, const AnimationPlayerComponent& component);

} // Engine
