#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

// c++
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	AnimationPlayerComponent enum class
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
	//	AnimationPlayerComponent struct
	//============================================================================
	// 名前付きで再生するクリップ1つ分の設定
	struct AnimationState {

		std::string name;
		AssetID clip{};
		float speed = 1.0f;
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

	// グループ内クリップ1つ分の再生状態
	struct AnimationClipRuntime {

		// グループ内のどのstateか
		std::string stateName;
		// クリップ内時間
		float time = 0.0f;
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

		// 以下はC#公開用のミラー
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
		// 再生開始時に捕捉した全プロパティのbase値
		std::vector<AnimationPreviewBaseValue> runtimeBaseValues;

		// C#からの再生要求(グループ名)、systemが立ち上がりで消費する
		std::string runtimePlayRequest;
		float runtimePlayFade = 0.0f;
		// C#からの停止要求
		bool runtimeStopRequest = false;
	};

	//============================================================================
	//	AnimationPlayerComponent functions
	//============================================================================
	void from_json(const nlohmann::json& in, AnimationPlayerComponent& component);
	void to_json(nlohmann::json& out, const AnimationPlayerComponent& component);

	ENGINE_REGISTER_COMPONENT(AnimationPlayerComponent, "AnimationPlayer");
} // Engine
