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
		ClampForever, // 終端値を保持して停止
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
	};

	struct AnimationPlayerComponent {

		// 有効か
		bool enabled = true;
		// 名前付きクリップ一覧
		std::vector<AnimationState> states;
		// 開始時に再生するstate名
		std::string defaultState;
		// Play開始時にdefaultStateを自動再生するか
		bool playOnStart = true;
		// Editモードでもプレビュー再生するか
		bool playInEditMode = false;
		// 全体の再生速度倍率
		float globalSpeed = 1.0f;

		// 再生中のstate名
		std::string runtimeCurrent;
		// 遷移元と遷移先のstate名
		std::string runtimeFrom;
		std::string runtimeTo;
		// 現在と遷移元のクリップ内時間
		float runtimeTime = 0.0f;
		float runtimeFromTime = 0.0f;
		// クロスフェードの進行0-1と所要時間
		float runtimeFade = 0.0f;
		float runtimeFadeDuration = 0.0f;
		// PingPong用の進行方向
		int8_t runtimeDir = 1;
		int8_t runtimeFromDir = 1;
		// 再生中か
		bool runtimePlaying = false;
		// クロスフェード中か
		bool runtimeInTransition = false;
		// 非ループ再生が終端へ達したか
		bool runtimeFinished = false;
		// playOnStartを消費済みか
		bool runtimeStarted = false;
		// base値を捕捉済みか
		bool runtimeBaseCaptured = false;
		// 再生開始時に捕捉した全プロパティのbase値
		std::vector<AnimationPreviewBaseValue> runtimeBaseValues;

		// C#からの再生要求、systemが立ち上がりで消費する
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
